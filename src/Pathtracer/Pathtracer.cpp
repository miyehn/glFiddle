#include "Pathtracer.hpp"
#include "Render/Mesh.h"
#include "Scene/MeshObject.h"
#include "BSDF.hpp"
#include "Scene/Light.hpp"
#include "PathtracerLight.hpp"
#include "Assets/ConfigAsset.hpp"
#include "Render/Materials/GltfMaterialInfo.h"
#include "CpuSkyAtmosphere/CpuSkyAtmosphere.h"
#include <stack>
#include <unordered_map>
#include <chrono>
#include <thread>
#include <atomic>
#include <condition_variable>
#if GRAPHICS_DISPLAY
#include <imgui.h>
#include "Render/Vulkan/VulkanUtils.h"
#include "Render/DebugDraw.h"
#include "Render/Texture.h"
#endif

// include this generated header to be able to use the kernels
#if ISPC
#include "pathtracer_kernel_ispc.h"
#endif
#include "Assets/SceneAsset.h"
#include "Scene/SkyAtmosphere/SkyAtmosphere.h"

struct RaytraceThread {

	enum Status { uninitialized, working, pending_upload, uploaded, ready_for_next, all_done };
	
	RaytraceThread(std::function<void(int)> work, int _tid) : tid(_tid) {
		thread = std::thread(work, _tid);
	}

	std::atomic<int> tid = 0;
	std::thread thread{};
	std::mutex m{};
	std::condition_variable cv{};

	std::atomic<bool> finished = true;
	std::atomic<Status> status = uninitialized;
	int tile_index = -1; // protected by m just like the tile buffer

};

Pathtracer::Pathtracer(uint32_t _width, uint32_t _height) {

	width = _width;
	height = _height;

#if GRAPHICS_DISPLAY
	initialized = false;
	enabled = false;
#endif
}

Pathtracer::~Pathtracer() {

#if GRAPHICS_DISPLAY
	if (!initialized) return;
	clear_tasks_and_threads_begin();
	clear_tasks_and_threads_wait();

	delete window_surface;
	viewInfoUbo.release();
	delete debugLines;
#endif

	delete image_buffer;
	for (uint32_t i=0; i<cached_config.NumThreads; i++) {
		delete subimage_buffers[i];
	}
	delete subimage_buffers;

	for (auto l : lights) delete l.light;
	for (auto t : primitives) delete t;

	delete bvh;

	delete cpuSky;

	// delete BSDF library
	for (auto& pair : BSDFs) {
		delete pair.second;
	}
	BSDFs.clear();

	TRACE("deleted pathtracer");
}

void Pathtracer::initialize() {
	TRACE("initializing pathtracer");

#if GRAPHICS_DISPLAY
	// subscribe itself to scene asset
	get_scene_asset()->begin_reload.emplace_back([this](){
		clear_tasks_and_threads_begin();
		clear_tasks_and_threads_wait();
	});
#endif

	// scene
	reload_scene(drawable);

	// define thread work lambda
	raytrace_task = [this](int tid)
	{
		while (true)
		{
			// What can cause this line to hit EXEC_BAD_ACCESS?
			std::unique_lock<std::mutex> lock(threads[tid]->m);

			// wait until main thread says it's okay to keep working (or okay to quit)
			threads[tid]->cv.wait(lock, [this, tid] {
				return threads[tid]->status == RaytraceThread::ready_for_next
				|| threads[tid]->status == RaytraceThread::all_done;
			});

			// try to get next tile to work on
			uint32_t tile;
			if (raytrace_tasks.dequeue(tile))
			{
				threads[tid]->status = RaytraceThread::working;
				threads[tid]->tile_index = tile;
				raytrace_tile(tid, tile);
				if (threads[tid]->status == RaytraceThread::all_done) {// modified by main thread while working
					//LOG("%d: notified to quit early while working", tid)
					break;
				} else {
					threads[tid]->status = RaytraceThread::pending_upload;
				}
			}
			else
			{
				//LOG("%d: quit bc no more work to do", tid)
				threads[tid]->status = RaytraceThread::all_done;
				break;
			}
		}
	};

#if GRAPHICS_DISPLAY
	// graphics api stuff
	ImageCreator windowSurfaceCreator(
		VK_FORMAT_R8G8B8A8_SRGB,
		{static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1},
		VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
		VK_IMAGE_ASPECT_COLOR_BIT,
		"Pathtracer window surface image");
	window_surface = new Texture2D(windowSurfaceCreator);

	viewInfoUbo = VmaBuffer({&Vulkan::Instance->memoryAllocator,
							sizeof(ViewInfo),
							VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
							VMA_MEMORY_USAGE_CPU_TO_GPU,
							"View info uniform buffer (path tracer)"});

	DescriptorSetLayout layout{};
	layout.addBinding(0, VK_SHADER_STAGE_ALL_GRAPHICS, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
	descriptorSet = DescriptorSet(layout);
	descriptorSet.pointToBuffer(viewInfoUbo, 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);

	debugLines = new DebugLines(descriptorSet.getLayout(), Vulkan::Instance->getSwapChainRenderPass());
#endif

	//-------- load config --------

	config = new ConfigAsset("config/pathtracer.ini", true, [this](const ConfigAsset* cfg) {

		uint32_t old_num_threads = cached_config.NumThreads;

		// read from file
#if ISPC
		cached_config.ISPC = cfg->lookup<int>("ISPC");
#endif
		cached_config.UseBVH = cfg->lookup<int>("UseBVH");

		cached_config.Multithreaded = cfg->lookup<int>("Multithreaded");
		cached_config.NumThreads = cfg->lookup<int>("NumThreads");
		cached_config.TileSize = cfg->lookup<int>("TileSize");

		cached_config.UseDirectLight = cfg->lookup<int>("UseDirectLight");
		cached_config.DirectLightSamples = cfg->lookup<int>("DirectLightSamples");

		cached_config.UseJitteredSampling = cfg->lookup<int>("UseJitteredSampling");
		cached_config.UseDOF = cfg->lookup<int>("UseDOF");
		cached_config.FocalDistance = cfg->lookup<float>("FocalDistance");
		cached_config.ApertureRadius = cfg->lookup<float>("ApertureRadius");

		cached_config.MaxRayDepth = cfg->lookup<int>("MaxRayDepth");
		cached_config.RussianRouletteThreshold = cfg->lookup<float>("RussianRouletteThreshold");

		cached_config.MinRaysPerPixel = cfg->lookup<int>("MinRaysPerPixel");

		// initialization related to config options

		tiles_X = std::ceil(float(width) / cached_config.TileSize);
		tiles_Y = std::ceil(float(height) / cached_config.TileSize);

#if GRAPHICS_DISPLAY
		// clear old threads
		clear_tasks_and_threads_begin();
		clear_tasks_and_threads_wait();
#endif

		// cpu buffers
		delete image_buffer;
		if (subimage_buffers /* not null if it's previously created at least once */) {
			for (uint32_t i=0; i<old_num_threads; i++) {
				delete subimage_buffers[i];
			}
			delete subimage_buffers;
		}
		image_buffer = new unsigned char[width * height * PATHTRACER_OUT_NUM_CHANNELS * PATHTRACER_OUT_SIZE_PER_CHANNEL];
		subimage_buffers = new unsigned char*[cached_config.NumThreads];
		for (int i=0; i<cached_config.NumThreads; i++) {
			subimage_buffers[i] = new unsigned char[
			cached_config.TileSize * cached_config.TileSize * PATHTRACER_OUT_NUM_CHANNELS *PATHTRACER_OUT_SIZE_PER_CHANNEL];
		}

		// queue tasks, spawn threads, etc.
		reset();
	});

#if GRAPHICS_DISPLAY
	initialized = true;
#endif
}

BSDF *Pathtracer::get_or_create_mesh_bsdf(const std::string &materialName)
{
	auto iter = BSDFs.find(materialName);
	GltfMaterialInfo* info = GltfMaterialInfo::get(materialName);
	EXPECT(info != nullptr, true)

	if (iter != BSDFs.end()) {
		auto pooled_bsdf = iter->second;
		if (pooled_bsdf->asset_version == info->_version) {
			return pooled_bsdf;
		} else {
			delete pooled_bsdf;
		}
	}

	// create a new one

	BSDF* bsdf;

	if (myn::lower(materialName) == "glass") {
		bsdf = new Glass();
		TRACE("created GLASS material")
	} else if (myn::lower(materialName) == "mirror") {
		bsdf = new Mirror();
		TRACE("created MIRROR material")
	} else {
		bsdf = new Diffuse(info->BaseColorFactor * 0.8f);
		TRACE("created new BSDF (%f, %f, %f)%s",
			  bsdf->albedo.r, bsdf->albedo.g, bsdf->albedo.b, bsdf->is_emissive ? " (emissive)" : "")
	}
	bsdf->asset_version = info->_version;
	bsdf->set_emission(info->EmissiveFactor);
	BSDFs[info->name] = bsdf;

	return bsdf;
}

void Pathtracer::reload_scene(SceneObject *scene) {

	primitives.clear();
	lights.clear();

	// also delete BSDF library
	for (auto& pair : BSDFs) {
		delete pair.second;
	}
	BSDFs.clear();

	delete bvh;
	bvh = new BVH(&primitives, 0);

	int meshes_count = 0;
	float light_power_sum = 0;
	PathtracerDirectionalLight* foundSun = nullptr;
	scene->foreach_descendent_bfs([&](SceneObject* drawable)
	{
		if (auto* mo = dynamic_cast<MeshObject*>(drawable)) {

			BSDF* bsdf = get_or_create_mesh_bsdf(mo->mesh->materialName);
			mo->bsdf = bsdf;
			meshes_count++;

			bool emissive = mo->bsdf->is_emissive;
			for (int i=0; i < mo->mesh->get_num_indices(); i+=3) {
				// loop and load triangles
				Vertex v1 = mo->mesh->get_vertices()[mo->mesh->get_indices()[i]];
				Vertex v2 = mo->mesh->get_vertices()[mo->mesh->get_indices()[i + 1]];
				Vertex v3 = mo->mesh->get_vertices()[mo->mesh->get_indices()[i + 2]];
				auto* T = new Triangle(mo->object_to_world(), v1, v2, v3, mo->bsdf);
				auto* P = static_cast<Primitive*>(T);
				primitives.push_back(P);

				// also load as light if emissive
				if (emissive) {
					auto L = new PathtracerMeshLight(T);
					float w = L->get_weight();
					light_power_sum += w;
					lights.push_back( {static_cast<PathtracerLight*>(L), w} );
				}
			}
		}
		else if (auto* plight = dynamic_cast<PointLight*>(drawable)) {
			auto L = new PathtracerPointLight(plight->world_position(),
											  plight->getMultipliedColor() * 4.0f * PI / PBR_WATTS_TO_LUMENS);
			float w = L->get_weight();
			light_power_sum += w;
			lights.push_back( {static_cast<PathtracerLight*>(L), w} );
		}
		else if (auto* dlight = dynamic_cast<DirectionalLight*>(drawable)) {
			auto L = new PathtracerDirectionalLight(dlight->getLightDirection(),
													dlight->getMultipliedColor() / PBR_WATTS_TO_LUMENS);
			float w = L->get_weight();
			light_power_sum += w;
			lights.push_back( {static_cast<PathtracerLight*>(L), w} );
			if (dlight == SkyAtmosphere::getInstance()->getSun()) {
				foundSun = L;
			}
		}
		else if (auto* sky = dynamic_cast<SkyAtmosphere*>(drawable)) {
			if (sky->enabled() && sky->getSun()) {
				cpuSky = new myn::sky::CpuSkyAtmosphere();
				cpuSky->renderingParams.dir2sun = -sky->getSun()->getLightDirection();
				cpuSky->renderingParams.cameraPosWS = camera->world_position();
				cpuSky->updateLuts();
				TRACE("created CPU sky")
			}
		}
	});

	// if sky atmosphere is created, modify sun somewhat:
	if (cpuSky) {
		EXPECT(foundSun != nullptr, true)
		foundSun->apply_sky(cpuSky);
	}

	// post-process light weights (normalize them)
	for (int i = 0; i < lights.size(); i++) {
		float normalized_w = lights[i].cumulative_weight / light_power_sum;
		lights[i].one_over_pdf = 1.0f / normalized_w;
		if (i == 0) {
			lights[i].cumulative_weight = normalized_w;
		} else {
			lights[i].cumulative_weight = lights[i - 1].cumulative_weight + normalized_w;
		}
	}

	bvh->primitives_start = 0;
	bvh->primitives_count = primitives.size();
	bvh->update_extents();
	bvh->expand_bvh();

	scene_version = get_scene_asset()->get_version();

	TRACE("loaded a scene with %d meshes, %llu triangles, %llu lights",
		  meshes_count, primitives.size(), lights.size());
}

void Pathtracer::reset() {
	TRACE("reset pathtracer");

	//-------- threading stuff --------
	if (cached_config.Multithreaded) {

		// enqueue all new tiles
		for (uint32_t i=0; i < tiles_X * tiles_Y; i++) {
			raytrace_tasks.enqueue(i);
		}
		// spawn new threads to start working on them
		for (uint32_t i=0; i<cached_config.NumThreads; i++) {
			threads.push_back(new RaytraceThread(raytrace_task, i));
		}
	}
	//---------------------------------
	rendered_tiles = 0;
	cumulative_render_time = 0.0f;
	generate_pixel_offsets();

	memset(image_buffer, 40, width * height * PATHTRACER_OUT_NUM_CHANNELS * PATHTRACER_OUT_SIZE_PER_CHANNEL);
#if GRAPHICS_DISPLAY
	paused = true;
	notified_pause_finish = true;
	finished = false;

	upload_rows(0, height);
#endif
}

#if GRAPHICS_DISPLAY
bool Pathtracer::handle_event(SDL_Event event) {
	if (event.type==SDL_KEYUP && event.key.keysym.sym==SDLK_SPACE && !finished) {
		if (paused) continue_trace();
		else {
			TRACE("pausing - waiting for pending tiles");
			pause_trace();
		}
		return true;

	} else if (event.type==SDL_MOUSEBUTTONUP && event.button.button==SDL_BUTTON_LEFT) {
		int x, y; // NOTE: y IS INVERSED!!!!!!!!!!!
		SDL_GetMouseState(&x, &y);
		const uint8* state = SDL_GetKeyboardState(nullptr);

		if (state[SDL_SCANCODE_LSHIFT]) {
			uint32_t pixel_index = (height-y) * width + x;
			raytrace_debug(pixel_index);

		} else if (state[SDL_SCANCODE_LALT]) {
			float d = depth_of_first_hit(x, height-y);
			cached_config.FocalDistance = d;
			TRACE("setting focal distance to %f", d);
		}
		return true;
	} else if (event.type==SDL_MOUSEBUTTONUP && event.button.button==SDL_BUTTON_RIGHT) {
		clear_debug_ray();
		return true;
	}

	return false;
}

void Pathtracer::on_selected() {
	if (!initialized) initialize();
	enabled = true;
	TRACE("pathtracer enabled");
	camera->lock();

	// update view info to buffer
	ViewInfo.ViewMatrix = camera->world_to_object();
	ViewInfo.ProjectionMatrix = camera->camera_to_clip();
	ViewInfo.ProjectionMatrix[1][1] *= -1; // so it's not upside down

	ViewInfo.CameraPosition = camera->world_position();
	ViewInfo.ViewDir = camera->forward();

	viewInfoUbo.writeData(&ViewInfo, sizeof(ViewInfo));
}

void Pathtracer::on_unselected() {
	enabled = false;
	TRACE("pathtracer disabled");
	camera->unlock();
}

void Pathtracer::pause_trace() {
	myn::TimePoint end_time = std::chrono::high_resolution_clock::now();
	cumulative_render_time += std::chrono::duration<float>(end_time - last_begin_time).count();
	TRACE("rendered %f seconds so far.", cumulative_render_time);
	notified_pause_finish = false;
	paused = true;
}

void Pathtracer::continue_trace() {
	TRACE("continue trace");
	last_begin_time = std::chrono::high_resolution_clock::now();
#if ISPC
	if (cached_config.ISPC) {
		load_ispc_data();
	}
#endif
	paused = false;
}

void Pathtracer::clear_tasks_and_threads_begin() {
	if (cached_config.Multithreaded) {
		raytrace_tasks.clear();
		for (auto & thread : threads) {
			thread->status = RaytraceThread::all_done;
			thread->cv.notify_all();
		}
	}
}

void Pathtracer::clear_tasks_and_threads_wait() {
	if (cached_config.Multithreaded) {
		for (auto & thread : threads) {
			if (thread->thread.joinable()) thread->thread.join();
		}
		threads.clear();
	}
}

void Pathtracer::render(VkCommandBuffer cmdbuf)
{
	if (!initialized) initialize();

	if (scene_version != get_scene_asset()->get_version()) {
		reload_scene(drawable);
		reset();
	}

	// update
	if (cached_config.Multithreaded // multithreaded c++
#if ISPC
		&& !cached_config.ISPC
#endif
	){
		if (!finished) {
			int uploaded_threads = 0;
			int finished_threads = 0;

			for (uint32_t i=0; i<threads.size(); i++) {

				if (threads[i]->status == RaytraceThread::all_done) {
					finished_threads++;
					if (threads[i]->thread.joinable()) threads[i]->thread.join();

				} else if (threads[i]->status == RaytraceThread::pending_upload) {
					std::lock_guard<std::mutex> lock(threads[i]->m);
					upload_tile(i, threads[i]->tile_index);
					threads[i]->status = RaytraceThread::uploaded;

				} else if (threads[i]->status == RaytraceThread::uploaded) {
					uploaded_threads++;
					if (!paused) {
						threads[i]->status = RaytraceThread::ready_for_next;
						threads[i]->cv.notify_all();
					}

				} else if (threads[i]->status == RaytraceThread::uninitialized) {
					if (!paused) {
						threads[i]->status = RaytraceThread::ready_for_next;
						threads[i]->cv.notify_all();
					}
				}

			}

			if (finished_threads == threads.size()) {
				TRACE("Done!");
				finished = true;
				pause_trace();
			} else if (paused && uploaded_threads == threads.size() && !notified_pause_finish) {
				TRACE("pending tiles finished");
				notified_pause_finish = true;
			}
		}

	}
	else
	{
		if (!paused) {
			if (rendered_tiles == tiles_X * tiles_Y) {
				TRACE("Done!");
				pause_trace();

			} else {

				// TODO: spawn a task to do this instead
				raytrace_tile(0, rendered_tiles);
				upload_tile(0, rendered_tiles);

				rendered_tiles++;
			}
		}
	}

	///////////////////// DISPLAY ////////////////////////

	// barrier source into transfer source
	vk::insertImageBarrier(cmdbuf, window_surface->resource.image,
						   {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
						   VK_PIPELINE_STAGE_TRANSFER_BIT,
						   VK_PIPELINE_STAGE_TRANSFER_BIT,
						   VK_ACCESS_TRANSFER_WRITE_BIT,
						   VK_ACCESS_TRANSFER_WRITE_BIT,
						   VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
						   VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
	vk::blitToScreen(
		cmdbuf,
		window_surface->resource.image,
		{0, 0, 0},
		{(int32_t)width, (int32_t)height, 1});
	vk::insertImageBarrier(cmdbuf, window_surface->resource.image,
						   {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
						   VK_PIPELINE_STAGE_TRANSFER_BIT,
						   VK_PIPELINE_STAGE_TRANSFER_BIT,
						   VK_ACCESS_TRANSFER_WRITE_BIT,
						   VK_ACCESS_TRANSFER_WRITE_BIT,
						   VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
						   VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

	if (debugLines && debugLines->numSegments() > 0) {
		Vulkan::Instance->beginSwapChainRenderPass(cmdbuf);
		descriptorSet.bind(cmdbuf, VK_PIPELINE_BIND_POINT_GRAPHICS, DSET_FRAMEGLOBAL, debugLines->getPipelineLayout());
		debugLines->bindAndDraw(cmdbuf);
		Vulkan::Instance->endSwapChainRenderPass(cmdbuf);
	}
}

void Pathtracer::draw_config_ui()
{
	// reset
	if (ImGui::Button("clear buffer")) {

#if GRAPHICS_DISPLAY
		// clear old threads
		clear_tasks_and_threads_begin();
		clear_tasks_and_threads_wait();
#endif
		reset();
	}
}
#endif

/*
 * singleton pattern, but makes sure will get one with specified stats
 */
Pathtracer *Pathtracer::get(uint32_t _w, uint32_t _h)
{
	static Pathtracer* renderer = nullptr;
	static uint32_t w, h;

	if (_w == 0 || _h == 0) {
		// just get whatever is already created
		return renderer;
	}

	if (renderer == nullptr || _w != w || _h != h) {
		delete renderer;

		w = _w; h = _h;
		renderer = new Pathtracer(w, h);
	}
	return renderer;
}

Asset* Pathtracer::get_scene_asset() {
	auto scene_source = Config->lookup<std::string>("SceneSource");
	auto scene_asset = Asset::find<SceneAsset>(scene_source);
	EXPECT(scene_asset==nullptr, false)
	return scene_asset;
}
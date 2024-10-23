#include "Scene/Scene.hpp"
#include "Scene/Camera.hpp"
#include "Scene/PathtracerController.h"
#include "Scene/RtxTriangle.h"
#include "Pathtracer/Pathtracer.hpp"
#include "Assets/ConfigAsset.hpp"
#include "Assets/SceneAsset.h"
#include "Render/Renderers/RayTracingRenderer.h"
#include "Render/Renderers/SimpleRenderer.h"
#include "Render/Texture.h"

#include "Render/Vulkan/VulkanUtils.h"
#include "Utils/DebugUI.h"

#include "Utils/myn/RenderDoc.h"

#include <SDL2/SDL.h>
#include <imgui.h>
#include <backends/imgui_impl_sdl.h>
#include <backends/imgui_impl_vulkan.h>

#include "Render/Renderers/DeferredRenderer.h"
#include "Assets/EnvironmentMapAsset.h"
#include "Scene/Probe.h"
#include "Scene/SkyAtmosphere/SkyAtmosphere.h"

using namespace myn;

#ifdef _WIN32
#ifdef main
#undef main
#endif
#endif

//////////////////////////////////////////////////////////////////

Camera* Camera::Active;
Scene* Scene::Active;

namespace
{
	uint32_t width = 1280;
	uint32_t height = 720;

	std::string name;

	SDL_Window* window;
	SDL_GLContext context;

	myn::TimePoint previous_time;

	bool show_imgui_demo = false;

	// other potentially temporary globals

	enum e_renderer {
		simple = 0,
		deferred = 1,
		pathtracer = 2,
		rtx = 3
	} renderer_index = e_renderer::simple;

	std::vector<Renderer*> renderers{};

}// fileprivate

static void init();

static bool process_input();

static void update(float elapsed);

static void draw();

static void cleanup();

//////////////////////////////////////////////////////////////////

static void init()
{
	vk::init_window("niar - main window", width, height, &window);

	{// shared resources
		LOG("loading resources (vulkan)...");
		Texture2D::createDefaultTextures();

		const libconfig::Setting& asset_paths = Config->lookupRaw("AdditionalAssets");
		int num_paths = asset_paths.getLength();
		for (int i = 0; i < num_paths; i++) {
			std::string path = asset_paths[i];
			new MeshAsset(path, "sphere");
		}
	}

	{// scene
		auto gltf = new Scene("SceneSource");

		// glb scene
		new SceneAsset(gltf, Config->lookup<std::string>("SceneSource"));
		gltf->add_child(new PathtracerController());

		Scene::Active = gltf;

		// find a camera and set it active
		Scene::Active->foreach_descendent_bfs([](SceneObject* obj) {
			auto cam = dynamic_cast<Camera*>(obj);
			if (cam) Camera::Active = cam;
		});
		if (!Camera::Active) {
			WARN("there's no active camera!")
		}

		// environment map
		if (Config->lookup<int>("LoadEnvironmentMap")) {
			new EnvironmentMapAsset(Config->lookup<std::string>("EnvironmentMap"));
		}

#ifdef WINOS
		// rtx
		if (Config->lookup<int>("Debug.RTX")) {
			auto tri = new RtxTriangle();
			gltf->add_child(tri);
			RayTracingRenderer::get()->outImage = tri->outImage;
		}
#endif

		// probe (debug)
		auto probe = new EnvMapVisualizer;
		probe->name = "Probe";
		gltf->add_child(probe);

		// sky atmosphere
		auto sky = SkyAtmosphere::getInstance();
		sky->name = "Sky Atmosphere";
		gltf->add_child(sky);
		if (!Config->lookup<int>("SkyAtmosphereDefaultEnabled")) {
			sky->toggle_enabled();
		}
	}

	ui::usePurpleStyle();
	ui::checkBox("show ImGui demo", &show_imgui_demo);

	// renderdoc
	if (Config->lookup<int>("Debug.RenderDoc"))
	{
		ui::button("capture frame", RenderDoc::captureNextFrame, "Rendering");
		ui::elem([](){ ImGui::SameLine(); }, "Rendering");
		ui::button("toggle renderdoc overlay", RenderDoc::toggleOverlay, "Rendering");
		ui::elem([](){ ImGui::Separator(); }, "Rendering");
	}

	{// renderers
		int rtx_enabled = Config->lookup<int>("Debug.RTX");
		renderers.push_back(SimpleRenderer::get());
		renderers.push_back(DeferredRenderer::get());
		renderers.push_back(Pathtracer::get(width, height));
#ifdef WINOS
		if (rtx_enabled) {
			renderers.push_back(RayTracingRenderer::get());
		}
#endif

		auto rendererIndexRef = (int*)&renderer_index;
		ui::elem([rendererIndexRef, rtx_enabled]()
		{
#ifdef WINOS
			if (rtx_enabled) {
				ImGui::Combo("renderer", rendererIndexRef, "Simple\0Deferred\0Pathtracer\0RTX\0\0");
			} else {
#endif
				ImGui::Combo("renderer", rendererIndexRef, "Simple\0Deferred\0Pathtracer\0\0");
#ifdef WINOS
			}
#endif
			ImGui::Separator();

			renderers[renderer_index]->draw_config_ui();

		}, "Rendering");
	}

	{// scene hierarchy
		static bool show_global_transform = false;
		ui::checkBox("show global transform", &show_global_transform, "Scene hierarchy (" + Scene::Active->name + ")");
		ui::elem([&]()
		{
			std::function<void(SceneObject* node)> make_tree = [&make_tree](SceneObject* node)
			{
				ImGui::SetNextItemOpen(node->ui_default_open, ImGuiCond_Once);
				if (ImGui::TreeNode(node->name.c_str()))
				{
					if (ImGui::Button(node->enabled() ? "disable" : "enable")) {
						node->toggle_enabled();
					}
					if (node->ui_show_transform) node->draw_transform_ui(show_global_transform);
					node->drawConfigUI();
					for (auto child : node->children) make_tree(child);
					ImGui::TreePop();
				}
			};
			for (auto child : Scene::Active->children) make_tree(child);
		}, "Scene hierarchy (" + Scene::Active->name + ")");
	}

}

static bool process_input()
{
	SDL_Event event;
	bool quit = false;
	while (SDL_PollEvent(&event)==1 && !quit)
	{
		// termination
		if (event.type == SDL_QUIT) { quit=true; break; }
		else if (event.type==SDL_KEYUP &&
				 event.key.keysym.sym==SDLK_ESCAPE) { quit=true; break; }

		// window gains focus
		else if (event.type==SDL_WINDOWEVENT && event.window.event==SDL_WINDOWEVENT_FOCUS_GAINED) {
			if (Config->lookup<int>("Debug.AutoHotReload")) {

				Asset::reload_all();

				// find a camera and set it active
				Scene::Active->foreach_descendent_bfs([](SceneObject* obj) {
					auto cam = dynamic_cast<Camera*>(obj);
					if (cam) Camera::Active = cam;
				});
			}
		}

		// imgui; pass on control to the rest of the app if event.type is not keyup
		ImGui_ImplSDL2_ProcessEvent(&event);

		// the rest of the app
		if (!ImGui::GetIO().WantCaptureMouse && !ImGui::GetIO().WantCaptureKeyboard) {
			Scene::Active->handle_event(event);
		}
	}
	return quit;
}

static void update(float elapsed)
{
	// camera
	if (Camera::Active &&
		!ImGui::GetIO().WantCaptureMouse &&
		!ImGui::GetIO().WantCaptureKeyboard)
	{
		Camera::Active->update_control(elapsed);
	}
	// scenes (only the active one updates)
	if (Scene::Active && Scene::Active->enabled()) {
		Scene::Active->update(elapsed);
	}
}

#define IMGUI 1

static void draw()
{
#if IMGUI
	ImGui_ImplVulkan_NewFrame();
	ImGui_ImplSDL2_NewFrame(window);
	ImGui::NewFrame();

	if (show_imgui_demo) ImGui::ShowDemoWindow();

	ui::drawUI();
#endif

	// renderer selection and configuration

	static Renderer* prev_renderer = nullptr;
	Renderer* renderer = renderers[renderer_index];
	renderer->camera = Camera::Active;
	renderer->drawable = Scene::Active;

	if (renderer != prev_renderer) {
		renderer->on_selected();
		if (prev_renderer) prev_renderer->on_unselected();
		prev_renderer = renderer;
	}

	// draw with current renderer

	auto cmdbuf = Vulkan::Instance->beginFrame();
	{
		renderer->render(cmdbuf);

#if IMGUI
		{
			SCOPED_DRAW_EVENT(cmdbuf, "ImGui UI")
			ImGui::Render();

			Vulkan::Instance->beginSwapChainRenderPass(cmdbuf);
			ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmdbuf);
			Vulkan::Instance->endSwapChainRenderPass(cmdbuf);
		}
#endif
	}
	Vulkan::Instance->endFrame();
}

static void cleanup()
{
	Vulkan::Instance->waitDeviceIdle();

	for (auto renderer : renderers) {
		delete renderer;
	}

	Asset::release_all();
	Asset::delete_all();

	delete Scene::Active;
	delete Vulkan::Instance;

	SDL_DestroyWindow(window);
	SDL_Quit();
}

int main(int argc, const char * argv[])
{
	std::srand(time(nullptr));

	Config = new ConfigAsset("config/global.ini", false);

	if (Config->lookup<int>("Debug.RenderDoc")) RenderDoc::load("niar");

	init();

	while(true)
	{
		TimePoint current_time = std::chrono::high_resolution_clock::now();
		float elapsed = std::chrono::duration<float>(current_time - previous_time).count();
		elapsed = std::min(0.1f, elapsed);
		previous_time = current_time;

		bool should_quit = process_input();
		if (should_quit) break;

		myn::RenderDoc::potentiallyStartCapture();
		update(elapsed);
		draw();
		myn::RenderDoc::potentiallyEndCapture();
	}

	cleanup();

	return 0;
}

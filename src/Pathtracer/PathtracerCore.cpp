#include "Pathtracer.hpp"
#include "PathtracerLight.hpp"
#include "Assets/ConfigAsset.hpp"
#include "BSDF.hpp"
#include "Primitive.hpp"
#include "Scene/Camera.hpp"
#include "CpuSkyAtmosphere/CpuSkyAtmosphere.h"
#include "Utils/myn/Sample.h"
#if GRAPHICS_DISPLAY
#include "Render/DebugDraw.h"
#endif

void Pathtracer::generate_pixel_offsets() {
	pixel_offsets.clear();
	uint32_t sqk = std::ceil(sqrt(cached_config.MinRaysPerPixel));
	uint32_t num_offsets = pow(sqk, 2);
	TRACE("generating %u pixel offsets", num_offsets);
	
	// canonical arrangement
	for (int j=0; j<sqk; j++) {
		for (int i=0; i<sqk; i++) {
			vec2 p;
			p.x = (i + (j + myn::sample::rand01()) / sqk) / sqk;
			p.y = (j + (i + myn::sample::rand01()) / sqk) / sqk;
			pixel_offsets.push_back(p);
		}
	}
	// shuffle canonical arrangement
	for (int j=0; j<sqk; j++) {
		int k = std::floor(j + myn::sample::rand01() * (sqk - j));
		for (int i=0; i<sqk; i++) {
			float tmp = pixel_offsets[j*sqk + i].x;
			pixel_offsets[j*sqk + i].x = pixel_offsets[k*sqk + i].x;
			pixel_offsets[k*sqk + i].x = tmp;
		}
	}
	for (int i=0; i<sqk; i++) {
		int k = floor(i + myn::sample::rand01() * (sqk - i));
		for (int j=0; j<sqk; j++) {
			float tmp = pixel_offsets[j*sqk + i].y;
			pixel_offsets[j*sqk + i].y = pixel_offsets[j*sqk + k].y;
			pixel_offsets[j*sqk + k].y = tmp;
		}
	}

}

void Pathtracer::generate_rays(std::vector<RayTask>& tasks, uint32_t index) {
	tasks.clear();

	uint32_t w = index % width;
	uint32_t h = height - index / width;
	float fov = camera->fov;
	float half_width = float(width) / 2.0f;
	float half_height = float(height) / 2.0f;
	// the raytraced image plane is at plane z = -1. Supposed k is its size in half.
	float k_y = tan(fov / 2.0f);
	float k_x = k_y * camera->aspect_ratio;

	RayTask task;
	Ray& ray = task.ray;
	bool jittered = cached_config.UseJitteredSampling;
	for (int i = 0; i < (jittered ? pixel_offsets.size() : cached_config.MinRaysPerPixel); i++) {
		vec2 offset = jittered ? pixel_offsets[i] : myn::sample::unit_square_uniform();

		ray.o = camera->world_position();
		ray.tmin = 0.0;
		ray.tmax = INF;

		// dx, dy: deviation from canvas center, normalized to range [-1, 1]
		float dx = (w + offset.x - half_width) / half_width;
		float dy = (h + offset.y - half_height) / half_height;

		vec3 d_unnormalized_c = vec3(k_x * dx, k_y * dy, -1);
		vec3 d_unnormalized_w = mat3(camera->object_to_world()) * d_unnormalized_c;
		ray.d = normalize(d_unnormalized_w);

		if (cached_config.UseDOF) {
			vec3 focal_p = ray.o + cached_config.FocalDistance * d_unnormalized_w;

			vec3 aperture_shift_cam = vec3(myn::sample::unit_disc_uniform() * cached_config.ApertureRadius, 0);
			vec3 aperture_shift_world = mat3(camera->object_to_world()) * aperture_shift_cam;
			ray.o = camera->world_position() + aperture_shift_world;
			ray.d = normalize(focal_p - ray.o);
		}

		tasks.push_back(task);
	}
}

vec3 Pathtracer::raytrace_pixel(uint32_t index) {
	std::vector<RayTask> tasks;
	generate_rays(tasks, index);

	vec3 result = vec3(0);
	for (auto & task : tasks) {
		trace_ray(task, 0, false);
		result += task.output;
		//result += clamp(task.output, vec3(0), vec3(1));
	}

	result *= 1.0f / float(tasks.size());
	result = clamp(result, vec3(0), vec3(1));
	return result;
}

#if GRAPHICS_DISPLAY
// DEBUG ONLY!!!
void Pathtracer::raytrace_debug(uint32_t index) {
	logged_rays.clear();
	logged_rays.push_back(camera->world_position());

	int w = index % width;
	int h = index / width;
	RayTask task;
	generate_one_ray(task, w, h);

	trace_ray(task, 0, true);
	vec3& color = task.output;
	LOG("result color: %f %f %f", color.x, color.y, color.z);
	
	// upload ray vertices
	debugLines->clear();
	for (int i = 1; i < logged_rays.size(); i++) {
		PointData ep1(logged_rays[i-1], {255, 220, 100, 255});
		PointData ep2(logged_rays[i], {255, 220, 100, 255});
		debugLines->addSegment(ep1, ep2);
	}
	debugLines->uploadVertexBuffer();
	LOG("--------------------------------------");
}

void Pathtracer::clear_debug_ray()
{
	logged_rays.clear();
	debugLines->clear();
}
#endif

void Pathtracer::generate_one_ray(RayTask& task, int x, int y) {

	Ray& ray = task.ray;

	ray.o = camera->world_position();
	ray.tmin = 0.0f;
	ray.tmax = INF;

	float fov = camera->fov;
	// dx, dy: deviation from canvas center, normalized to range [-1, 1]
	vec2 offset = vec2(0.5f, 0.5f);
	float half_width = float(width) / 2.0f;
	float half_height = float(height) / 2.0f;
	float dx = (x + offset.x - half_width) / half_width;
	float dy = (y + offset.y - half_height) / half_height;
	// the ray traced image plane is at plane z = -1. Supposed k is its size in half.
	float k_y = tan(fov / 2.0f);
	float k_x = k_y * camera->aspect_ratio;

	vec3 d_cam = normalize(vec3(k_x * dx, k_y * dy, -1));
	ray.d = mat3(camera->object_to_world()) * d_cam;
}

#if GRAPHICS_DISPLAY
float Pathtracer::depth_of_first_hit(int x, int y) {
	RayTask task;
	generate_one_ray(task, x, y);
	
	// info of closest hit
	double t; vec3 n;
	bvh->intersect_primitives(task.ray, t, n, cached_config.UseBVH);

	t *= dot(task.ray.d, camera->forward());

	return float(t);
}
#endif

namespace
{
void make_h2w(mat3& h2w, const vec3& z) { // TODO: make more robust
	// choose a vector different from z
	vec3 tmp = normalize(vec3(1, 2, 3));

	vec3 x = cross(tmp, z);
	x = normalize(x);
	vec3 y = cross(z, x);

	h2w = mat3(x, y, z);
}

// see: https://stackoverflow.com/questions/687261/converting-rgb-to-grayscale-intensity
inline float brightness(const vec3& color) {
	return 0.2989f * color.r + 0.587f * color.g + 0.114f * color.b;
}
};

void Pathtracer::select_random_light(PathtracerLight* &light, float &one_over_pdf) {
	float rnd = myn::sample::rand01();
	LightAndWeight lw = {
		.light = nullptr,
		.cumulative_weight = rnd,
		.one_over_pdf = 1
	};
	auto it = std::lower_bound(lights.begin(), lights.end(), lw);
	if (it == lights.end()) {
		light = lights.back().light;
		one_over_pdf = lights.back().one_over_pdf;
	} else {
		light = it->light;
		one_over_pdf = it->one_over_pdf;
	}
}

void Pathtracer::trace_ray(RayTask& task, int ray_depth, bool debug) {
	if (ray_depth >= cached_config.MaxRayDepth) return;

	Ray& ray = task.ray;

	// info of closest hit
	double t; vec3 n;
	Primitive* primitive = bvh->intersect_primitives(ray, t, n, cached_config.UseBVH);

	if (primitive) { // intersected with at least 1 primitive (has valid t, n, bsdf)

		const BSDF *bsdf = primitive->bsdf;
		// pre-compute (or declare) some common things to be used later
		vec3 L = vec3(0);
		vec3 hit_p = ray.o + float(t) * ray.d;
#if GRAPHICS_DISPLAY
		if (debug) logged_rays.push_back(hit_p);
#endif
		// construct transform from hemisphere space to world space;
		mat3 h2w;
		make_h2w(h2w, n);
		mat3 w2h = transpose(h2w);
		// wi, wo
		vec3 wo_world = -ray.d;
		vec3 wo_hemi = -w2h * ray.d;
		// 
		vec3 wi_world; // to be transformed from wi_hemi
		vec3 wi_hemi; // to be assigned by f
		float costhetai; // some variation of dot(wi_world, n)

		//---- emission ----
		if (cached_config.UseDirectLight) {
			// totally a hack...
			if (ray_depth == 0 || ray.receive_le || !bsdf->is_emissive) L += bsdf->get_emission();
		} else {
			L += bsdf->get_emission();
		}

		if (cached_config.UseDirectLight && !lights.empty()) {
			//---- direct light contribution ----
			if (!bsdf->is_delta) {

				float each_sample_weight = 1.0f / (float) cached_config.DirectLightSamples;
				for (uint32_t i = 0; i < cached_config.DirectLightSamples; i++) {

					PathtracerLight *light;
					float one_over_pdf;
					select_random_light(light, one_over_pdf);

					Ray ray_to_light;
					float attenuation;
					ray_to_light.o = hit_p;
					light->ray_to_light_and_attenuation(ray_to_light, attenuation);

					double tmp_t;
					vec3 tmp_n;
					bool in_shadow =
						bvh->intersect_primitives(ray_to_light, tmp_t, tmp_n, cached_config.UseBVH) != nullptr;
					if (!in_shadow) {
						wi_world = ray_to_light.d;
						wi_hemi = w2h * wi_world;
						costhetai = std::max(0.0f, dot(n, wi_world));
						vec3 L_direct = light->get_emission() * bsdf->f(wi_hemi, wo_hemi) * costhetai * attenuation
										* one_over_pdf * each_sample_weight;
						// correction for when above num and denom both 0. TODO: is this right?
						if (glm::isnan(L_direct.x) || glm::isnan(L_direct.y) || glm::isnan(L_direct.z)) L_direct = vec3(0);
						L += L_direct;
					}
				}

			}
		}
		task.output += task.contribution * L;

#if 1 // indirect lighting (recursive)

		if ((cached_config.UseDirectLight && !bsdf->is_emissive) ||
			 !(cached_config.UseDirectLight)) {
#if GRAPHICS_DISPLAY
			if (debug) {
				LOG("---- hit at depth %d at (%f %f %f) ----", ray_depth, hit_p.x, hit_p.y, hit_p.z);
			}
#endif

			float pdf;
			vec3 f = bsdf->sample_f(pdf, wi_hemi, wo_hemi, debug);

			// transform wi back to world space
			wi_world = h2w * wi_hemi;
			costhetai = abs(dot(n, wi_world));
#if GRAPHICS_DISPLAY
			if (debug) {
				LOG("wo: %f %f %f (normalized to %f %f %f)", 
						wo_world.x, wo_world.y, wo_world.z, wo_hemi.x, wo_hemi.y, wo_hemi.z);
				LOG("wi: %f %f %f (normalized to %f %f %f)", 
						wi_world.x, wi_world.y, wi_world.z, wi_hemi.x, wi_hemi.y, wi_hemi.z);
			}
#endif
			// russian roulette
			float termination_prob = 0.0f;
			ray.rr_contribution *= brightness(f) * costhetai;
			if (ray.rr_contribution < cached_config.RussianRouletteThreshold) {
				termination_prob = (cached_config.RussianRouletteThreshold - ray.rr_contribution)
					/ cached_config.RussianRouletteThreshold;
			}
			bool terminate = myn::sample::rand01() < termination_prob;

			// recursive step: trace scattered ray in wi direction (if not terminated by RR)
			vec3 Li = vec3(0);
			if (!terminate) {
				vec3 refl_offset = wi_hemi.z > 0 ? EPSILON * n : -EPSILON * n;
				Ray ray_refl(hit_p + refl_offset, wi_world); // alright I give up fighting epsilon for now...
				if (cached_config.UseDirectLight && bsdf->is_delta) ray_refl.receive_le = true;
				task.ray = ray_refl;
				task.contribution *= f * costhetai / pdf * (1.0f / (1.0f - termination_prob));
				// if it has some termination probability, weigh it more if it's not terminated
				trace_ray(task, ray_depth + 1, debug);
			}
#if GRAPHICS_DISPLAY
			else if (debug) {
				LOG("terminated by russian roulette");
			}
#endif
		}
#endif
#if GRAPHICS_DISPLAY
		if (debug) LOG("level %d returns: (%f %f %f)", ray_depth, L.x, L.y, L.z);
#endif
	}
	else {// ray missed
		if (cpuSky) {
			task.output += task.contribution * cpuSky->sampleSkyColor(ray.d);
		}
		else if (Config->lookup<int>("LoadEnvironmentMap")) {
			auto envmap = Asset::find<EnvironmentMapAsset>(Config->lookup<std::string>("EnvironmentMap"));
			task.output += task.contribution * myn::sample::tex::longlatmap_float3(
				(float*)(envmap->texels3x32.data()), envmap->width, envmap->height, ray.d);
		}
	}
}
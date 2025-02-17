#include "BSDF.hpp"
#include "Utils/myn/Misc.h"
#include "Utils/myn/Log.h"
#include "Utils/myn/Sample.h"

#define SQ(x) (x * x)
#define EMISSIVE_THRESHOLD SQ(0.4f)

#define USE_COS_WEIGHED 1

using namespace glm;

bool BSDF::compute_is_emissive() const {
	return dot(Le, Le) >= EMISSIVE_THRESHOLD;
}

vec3 Diffuse::f(const vec3& wi, const vec3& wo, bool debug) const {
	return albedo * ONE_OVER_PI;
}

vec3 Diffuse::sample_f(float& pdf, vec3& wi, vec3 wo, bool debug) const {
#if USE_COS_WEIGHED
	wi = myn::sample::hemisphere_cos_weighed();
	pdf = wi.z * ONE_OVER_PI;
#else
	wi = sample::hemisphere_uniform();
	pdf = ONE_OVER_TWO_PI;
#endif
	return f(wi, wo, debug);
}

vec3 Mirror::f(const vec3& wi, const vec3& wo, bool debug) const {
	return vec3(0.0f);
}

vec3 Mirror::sample_f(float& pdf, vec3& wi, vec3 wo, bool debug) const {
	wi = -wo;
	wi.z = wo.z;
	pdf = 1.0f;
	return albedo * (1.0f / wi.z);
}

vec3 Glass::f(const vec3& wi, const vec3& wo, bool debug) const {
	return vec3(0.0f);
}

vec3 Glass::sample_f(float& pdf, vec3& wi, vec3 wo, bool debug) const {
	// will treat wo as in direction and wi as out direction, since it's bidirectional

	bool trace_out = wo.z < 0; // the direction we're going to trace is into the medium

	// IOR, assume container medium is air
	float ni = trace_out ? IOR : 1.0f;
	float nt = trace_out ? 1.0f : IOR;
	if (debug) LOG("ni: %f, nt: %F", ni, nt);

	float cos_theta_i = abs(wo.z);
	float sin_theta_i = sqrt(1.0f - pow(cos_theta_i, 2));

	// opt out early for total internal reflection
	float cos_sq_theta_t = 1.0f - pow(ni/nt, 2) * (1.0f - wo.z * wo.z);
	bool TIR = cos_sq_theta_t < 0;
	if (TIR) { // total internal reflection
		if (debug) LOG("TIR");
		wi = -wo;
		wi.z = wo.z;
		pdf = 1.0f;
		return albedo * (1.0f / abs(wi.z));
	}
	
	// then use angles to find reflectance
	float r0 = pow((ni-nt) / (ni+nt), 2);
	float reflectance = r0 + (1.0f - r0) * pow(1.0f - cos_theta_i, 5);
	
	// flip a biased coin to decide whether to reflect or refract
	bool reflect = myn::sample::rand01() <= reflectance;
	if (reflect) {
		if (debug) LOG("reflect");
		wi = -wo;
		wi.z = wo.z;
		pdf = reflectance;
		return albedo * (reflectance / cos_theta_i);

	} else { // refract
		if (debug) LOG("refract")
		// remember we treat wi as "out direction"
		float cos_theta_t = sqrt(cos_sq_theta_t);
		float sin_theta_t = sqrt(1.0f - cos_sq_theta_t);

		float xy_norm_factor = sin_theta_t / sin_theta_i;
		wi = vec3(-wo.x * xy_norm_factor,
							-wo.y * xy_norm_factor,
							trace_out ? cos_theta_t : -cos_theta_t);

		pdf = 1.0f - reflectance;
		// now compute f...
		return albedo * ((nt*nt) / (ni*ni)) * (1.0f - reflectance) * (1.0f / cos_theta_i);
	}
}

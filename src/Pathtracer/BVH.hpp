#pragma once
#include "Primitive.hpp"

using namespace glm;

struct BVH
{
	BVH(std::vector<Primitive*>* _primitives_ptr, uint _depth) {
		depth = _depth;
		min = vec3(INF);
		max = vec3(-INF);
		primitives_ptr = _primitives_ptr;
		primitives_start = 0;
		primitives_count = 0;
		left = nullptr;
		right = nullptr;
	}
	~BVH() {
		if (left) delete left;
		if (right) delete right;
	}

	uint depth;

	vec3 min;
	vec3 max;

	std::vector<Primitive*>* primitives_ptr;
	uint primitives_start;
	uint primitives_count;
	BVH* left;
	BVH* right;

	void extend_primitive(Primitive* prim);
	
	void update_extents();
	void expand_bvh();

	float surface_area();
	bool intersect_aabb(const Ray& ray, float& tmin, float& tmax);
	Primitive* intersect_primitives(Ray& ray, double& t, vec3& n, bool use_bvh = true);
};
//
// Created by raind on 6/9/2022.
//

#include "MeshObject.h"
#include "Render/Mesh.h"

MeshObject::MeshObject(Mesh *in_mesh) : mesh(in_mesh)
{
	name = in_mesh->name;
	generate_aabb();
}

MeshObject::~MeshObject()
{
	delete mesh;
}

#if GRAPHICS_DISPLAY
void MeshObject::update(float elapsed)
{
	SceneObject::update(elapsed);
	locked = true;
}

void MeshObject::draw(VkCommandBuffer cmdbuf)
{
	mesh->draw(cmdbuf);
}
#endif

void MeshObject::set_local_position(glm::vec3 in_local_position)
{
	if (!locked) {
		_local_position = in_local_position;
		generate_aabb();
		//get_scene()->generate_aabb();
	}
}

void MeshObject::setRotation(glm::quat in_rotation)
{
	if (!locked) {
		_rotation = in_rotation;
		generate_aabb();
		//get_scene()->generate_aabb();
	}
}

void MeshObject::set_scale(glm::vec3 in_scale)
{
	if (!locked) {
		_scale = in_scale;
		generate_aabb();
		//get_scene()->generate_aabb();
	}
}

void MeshObject::generate_aabb()
{
	glm::mat4 o2w = object_to_world();
	aabb = AABB();
	for (int i=0; i<mesh->get_num_vertices(); i++) {
		aabb.add_point(o2w * glm::vec4(mesh->get_vertices()[i].position, 1));
	}
}

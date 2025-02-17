#pragma once
#include <string>
#include <functional>
#include <vector>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#if GRAPHICS_DISPLAY
#include <SDL_events.h>
#include <vulkan/vulkan.h>
#endif

class Scene;

class SceneObject {
public:

	explicit SceneObject(
			SceneObject* _parent = nullptr,
			std::string _name = "[unnamed scene object]");
	virtual ~SceneObject();

#if GRAPHICS_DISPLAY
	// inherited
	virtual bool handle_event(SDL_Event event);
	virtual void update(float elapsed);

	// draw function
	virtual void draw(VkCommandBuffer cmdbuf);
#endif

	// hierarchy operations
	SceneObject* parent;
	std::vector<SceneObject*> children = std::vector<SceneObject*>();
	void foreach_descendent_bfs(
		const std::function<void(SceneObject*)>& fn,
		const std::function<bool(SceneObject*)>& filter_condition = [](SceneObject* obj) {return true;});
	void set_parent(SceneObject* in_parent);
	bool add_child(SceneObject* child);
	bool try_remove_child(SceneObject* child);

	// other operations

	virtual void set_local_position(glm::vec3 local_position) { _local_position = local_position; }
	virtual void setRotation(glm::quat rotation) { _rotation = rotation; }
	virtual void set_scale(glm::vec3 scale) { _scale = scale; }

	void rotate_around_axis(glm::vec3 ws_axis_unitvec, float radians);

#if GRAPHICS_DISPLAY
	bool ui_default_open = false;
	bool ui_show_transform = true;
	void draw_transform_ui(bool global) const;
	virtual void drawConfigUI() {};
#endif

	// transformation
	glm::mat4 object_to_parent() const;
	glm::mat4 object_to_world() const;
	glm::mat4 parent_to_object() const;
	glm::mat4 world_to_object() const;
	
	glm::mat3 object_to_world_rotation() const;
	glm::mat3 world_to_object_rotation() const;

	glm::vec3 world_position() const;
	glm::vec3 local_position() const { return _local_position; }
	glm::quat rotation() const { return _rotation; }
	glm::vec3 scale() const { return _scale; }

	bool enabled() const { return _enabled; }
	void toggle_enabled();

	std::string name;

protected:

	glm::vec3 _local_position;
	glm::quat _rotation; // {w, x, y, z}
	glm::vec3 _scale;

	virtual void on_enable() {}
	virtual void on_disable() {}

	bool _enabled = true;
};
#pragma once
#include <vulkan/vulkan.h>
#include <unordered_map>
#include <string>

class ShaderModule
{
public:
	static ShaderModule* get(const std::string &path);

	VkShaderModule module;
private:
	explicit ShaderModule(const std::string &path);
	static std::unordered_map<std::string, ShaderModule*> pool;
};


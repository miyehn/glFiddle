//
// Created by raind on 5/21/2022.
//

#include "Asset.h"
#include "Utils/myn/Log.h"
#include "SceneAsset.h"
#include "EnvironmentMapAsset.h"
#include <filesystem>
#include <unordered_map>

#ifdef MACOS
#define to_time_t(diff) std::chrono::duration<time_t, std::ratio<86400>>(diff).count()
#endif

time_t get_file_clock_now() {
#ifdef MACOS
	auto epoch = std::chrono::file_clock::time_point();
	auto now = std::chrono::file_clock::now();
	return to_time_t(now - epoch);
#else
	auto tp = std::chrono::system_clock::now();
	return std::chrono::system_clock::to_time_t(tp);
#endif
}

time_t get_last_write_time(const std::string& path)
{
	auto file_time = std::filesystem::last_write_time(path);
#ifdef MACOS
	auto epoch = std::chrono::file_clock::time_point();
	return to_time_t(file_time - epoch);
#else
	auto system_time = std::chrono::clock_cast<std::chrono::system_clock>(file_time);
	return std::chrono::system_clock::to_time_t(system_time);
#endif
}
#ifdef MACOS
#undef to_time_t
#endif

std::unordered_map<std::string, Asset*> Asset::assets_pool;

Asset::Asset(const std::string &_path, const std::function<void()> &_load_action)
{
	relative_path = _path;
	load_action_internal = _load_action;
	reload_condition = [](){ return true; };
	assets_pool[relative_path] = this;
	if (load_action_internal) reload();
}

void Asset::reload() {
	time_t last_write_time = get_last_write_time(ROOT_DIR"/" + relative_path);
	if (last_load_time < last_write_time) {
		if (!_initialized || reload_condition()) {
			// begin reload callbacks
			for (auto& fn : begin_reload) fn();
			// reload
			last_load_time = get_file_clock_now();
			if (_initialized) bump_version();
			ASSET("loading asset '%s (now at v%d)'", relative_path.c_str(), _version)
			load_action_internal();
			_initialized = true;
			// finish reload callbacks
			for (auto& fn : finish_reload) fn();
		} else {
			WARN("'%s' was edited but not reloaded: condition not met", relative_path.c_str())
		}
	}
}

Asset::~Asset() {
	ASSERT(!_initialized)
	assets_pool.erase(relative_path);
}

void Asset::release_resources() {
	if (_initialized) {
		ASSET("releasing asset %s", relative_path.c_str())
	}
	_initialized = false;
}

void Asset::reload_all() {
	for (auto& p : assets_pool) {
		p.second->reload();
	}
}

void Asset::release_all() {
	for (const auto& pair : assets_pool) {
		auto asset = pair.second;
		if (asset) {
			asset->release_resources();
		}
	}
}

void Asset::delete_all() {
	std::vector<Asset*> assets;
	for (const auto& pair : assets_pool) {
		assets.push_back(pair.second);
	}
	for (const auto& asset : assets) {
		delete asset;
	}
	assets_pool.clear();
}

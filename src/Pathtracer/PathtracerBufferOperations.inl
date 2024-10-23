
#if ISPC
struct ISPC_Data
{
	std::vector<ispc::Camera> camera;
	std::vector<glm::vec2> pixel_offsets;
	uint32_t num_offsets;
	std::vector<ispc::Triangle> triangles;
	std::vector<ispc::BSDF> bsdfs;
	std::vector<uint32_t> area_light_indices;
	uint32_t num_triangles;
	uint32_t num_area_lights;
	//uint8_t* output;
	uint32_t width;
	uint32_t height;
	uint32_t tile_size;
	uint32_t num_threads;
	uint32_t max_ray_depth;
	float rr_threshold;
	bool use_direct_light;
	uint32_t area_light_samples;
	std::vector<ispc::BVH> bvh_root;
	uint32_t bvh_stack_size;
	bool use_bvh;
	bool use_dof;
	float focal_distance;
	float aperture_radius;
};

void Pathtracer::load_ispc_data() {

	if (ispc_data != nullptr) {
		delete(ispc_data);
	}
	ispc_data = new ISPC_Data();

	auto ispc_vec3 = [](const vec3& v) {
		ispc::vec3 res;
		res.x = v.x; res.y = v.y; res.z = v.z;
		return res;
	};

	// construct scene representation (triangles + materials list)
	ispc_data->bsdfs.resize(primitives.size());
	ispc_data->triangles.resize(primitives.size());
	for (int i=0; i<primitives.size(); i++)
	{
		ispc::Triangle &T = ispc_data->triangles[i];
		Triangle* T0 = dynamic_cast<Triangle*>(primitives[i]);
		if (!T0) ERR("failed to cast primitive to triangle?");
		// construct its corresponding material
		T.bsdf_index = i;
		ispc_data->bsdfs[i].albedo = ispc_vec3(T0->bsdf->albedo);
		ispc_data->bsdfs[i].Le = ispc_vec3(T0->bsdf->get_emission());
		ispc_data->bsdfs[i].is_delta = T0->bsdf->is_delta;
		ispc_data->bsdfs[i].is_emissive = T0->bsdf->is_emissive;
		if (T0->bsdf->type == BSDF::Mirror) {
			ispc_data->bsdfs[i].type = ispc::Mirror;
		} else if (T0->bsdf->type == BSDF::Glass) {
			ispc_data->bsdfs[i].type = ispc::Glass;
		} else {
			ispc_data->bsdfs[i].type = ispc::Diffuse;
		}
		// construct the ispc triangle object
		for (int j=0; j<3; j++) {
			T.vertices[j] = ispc_vec3(T0->vertices[j]);
			T.enormals[j] = ispc_vec3(T0->enormals[j]);
		}
		T.plane_n = ispc_vec3(T0->plane_n);
		T.plane_k = T0->plane_k;
		T.area = T0->area;
	}
	ispc_data->num_triangles = primitives.size();

	ispc_data->area_light_indices.resize(lights.size());
	uint light_count = 0;
	for (auto & light : lights) {
		if (!light.light->is_delta()) {
			Triangle* T = dynamic_cast<PathtracerMeshLight*>(light.light)->triangle;
			auto it = find(primitives.begin(), primitives.end(), T);
			if (it != primitives.end()) { // found
				ispc_data->area_light_indices[light_count] = it - primitives.begin();
			}
			light_count++;
		}
	}
	ispc_data->num_area_lights = light_count;

	// construct camera
	ispc_data->camera.resize(1);
	mat3 c2wr = mat3(camera->object_to_world());
	ispc_data->camera[0].camera_to_world_rotation.colx = ispc_vec3(c2wr[0]);
	ispc_data->camera[0].camera_to_world_rotation.coly = ispc_vec3(c2wr[1]);
	ispc_data->camera[0].camera_to_world_rotation.colz = ispc_vec3(c2wr[2]);
	ispc_data->camera[0].position = ispc_vec3(camera->world_position());
	ispc_data->camera[0].fov = camera->fov;
	ispc_data->camera[0].aspect_ratio = camera->aspect_ratio;

	// pixel offsets
	ispc_data->pixel_offsets = pixel_offsets;
	ispc_data->num_offsets = pixel_offsets.size();

	// BVH
	uint max_depth = 0;
	std::stack<BVH*> st;
	std::unordered_map<BVH*, int> m;
	// first iteration: make the structs, and map from node to index
	st.push(bvh);
	while (!st.empty()) {
		BVH* ptr = st.top(); st.pop();
		int self_index = ispc_data->bvh_root.size();
		m[ptr] = self_index;
		max_depth = glm::max(max_depth, ptr->depth);
		// make the node (except children indices)
		ispc::BVH node;
		node.min = ispc_vec3(ptr->min);
		node.max = ispc_vec3(ptr->max);
		node.triangles_start = ptr->primitives_start;
		node.triangles_count = ptr->primitives_count;
		node.self_index = self_index;
		ispc_data->bvh_root.push_back(node);
		// push children
		if (ptr->left) st.push(ptr->right);
		if (ptr->right) st.push(ptr->left);
	}
	// second iteration: set children indices
	st.push(bvh);
	while (!st.empty()) {
		BVH* ptr = st.top(); st.pop();
		bool is_leaf = !ptr->left || !ptr->right;
		int self_index = m[ptr];
		int left_index = is_leaf ? -1 : m[ptr->left];
		int right_index = is_leaf ? -1 : m[ptr->right];
		ispc_data->bvh_root[self_index].left_index = left_index;
		ispc_data->bvh_root[self_index].right_index = right_index;

		// push children
		if (ptr->left) st.push(ptr->right);
		if (ptr->right) st.push(ptr->left);
	}

	// and the rest of the inputs
	ispc_data->width = width;
	ispc_data->height = height;
	ispc_data->tile_size = cached_config.TileSize;
	ispc_data->num_threads = cached_config.Multithreaded ? cached_config.NumThreads : 1;
	ispc_data->max_ray_depth = cached_config.MaxRayDepth;
	ispc_data->rr_threshold = cached_config.RussianRouletteThreshold;
	ispc_data->use_direct_light = cached_config.UseDirectLight;
	ispc_data->area_light_samples = cached_config.DirectLightSamples;
	ispc_data->bvh_stack_size = (1 + max_depth) * 2;
	ispc_data->use_bvh = cached_config.UseBVH;
	ispc_data->use_dof = cached_config.UseDOF;
	ispc_data->focal_distance = cached_config.FocalDistance;
	ispc_data->aperture_radius = cached_config.ApertureRadius;

	TRACE("reloaded ISPC data");
}
#endif

void Pathtracer::set_mainbuffer_rgb(uint32_t i, vec3 rgb) {
	uint32_t pixel_size = NUM_CHANNELS * SIZE_PER_CHANNEL;
	image_buffer[pixel_size * i] = char(rgb.r * 255.0f);
	image_buffer[pixel_size * i + 1] = char(rgb.g * 255.0f);
	image_buffer[pixel_size * i + 2] = char(rgb.b * 255.0f);
	image_buffer[pixel_size * i + 3] = 255;
}

void Pathtracer::set_subbuffer_rgb(uint32_t buf_i, uint32_t i, vec3 rgb) {
	uint32_t pixel_size = NUM_CHANNELS * SIZE_PER_CHANNEL;
	unsigned char* buf = subimage_buffers[buf_i];
	buf[pixel_size * i] = char(rgb.r * 255.0f);
	buf[pixel_size * i + 1] = char(rgb.g * 255.0f);
	buf[pixel_size * i + 2] = char(rgb.b * 255.0f);
	buf[pixel_size * i + 3] = 255;
}

vec3 gamma_correct(vec3 in) {
	const vec3 gamma(0.455f);
	return pow(in, gamma);
}

void Pathtracer::raytrace_tile(uint32_t tid, uint32_t tile_index) {
	uint32_t X = tile_index % tiles_X;
	uint32_t Y = tile_index / tiles_X;

	uint32_t tile_size = cached_config.TileSize;

	uint32_t tile_w = std::min(tile_size, width - X * tile_size);
	uint32_t tile_h = std::min(tile_size, height - Y * tile_size);

	uint32_t x_offset = X * tile_size;
	uint32_t y_offset = Y * tile_size;

#if ISPC
	if (cached_config.ISPC)
	{
		// dispatch task to ispc
		ispc::raytrace_scene_ispc(
			ispc_data->camera.data(),
			(float*)ispc_data->pixel_offsets.data(),
			ispc_data->num_offsets,
			ispc_data->triangles.data(),
			ispc_data->bsdfs.data(),
			ispc_data->area_light_indices.data(),
			ispc_data->num_triangles,
			ispc_data->num_area_lights,
			subimage_buffers[tid],
			ispc_data->width,
			ispc_data->height,
			true,
			ispc_data->tile_size,
			tile_w, tile_h,
			X, Y,
			ispc_data->num_threads,
			ispc_data->max_ray_depth,
			ispc_data->rr_threshold,
			ispc_data->use_direct_light,
			ispc_data->area_light_samples,
			ispc_data->bvh_root.data(),
			ispc_data->bvh_stack_size,
			ispc_data->use_bvh,
			ispc_data->use_dof,
			ispc_data->focal_distance,
			ispc_data->aperture_radius);
	}
	else
#endif
	{
		for (uint32_t y = 0; y < tile_h; y++) {
			for (uint32_t x = 0; x < tile_w; x++) {

				uint32_t px_index_main = width * (y_offset + y) + (x_offset + x);
				vec3 color = raytrace_pixel(px_index_main);

				// do gamma correction BEFORE converting to R8G8B8A8 to avoid banding
				color = gamma_correct(color);

				set_mainbuffer_rgb(px_index_main, color);

				uint32_t px_index_sub = y * tile_w + x;
				set_subbuffer_rgb(tid, px_index_sub, color);

			}
		}
	}

}

#if GRAPHICS_DISPLAY

void Pathtracer::upload_rows(uint32_t begin, uint32_t rows)
{
	uint32_t subimage_offset = width * begin * NUM_CHANNELS * SIZE_PER_CHANNEL;
	uint8_t* data = image_buffer + subimage_offset;
	vk::uploadPixelsToImage(
		data,
		0, begin,
		width, rows,
		NUM_CHANNELS *SIZE_PER_CHANNEL,
		window_surface->resource
		);

	int percentage = int(float(begin + rows) / float(height) * 100.0f);
	TRACE("refresh! updated %d rows, %d%% done.", rows, percentage);
}

void Pathtracer::upload_tile(uint32_t subbuf_index, uint32_t begin_x, uint32_t begin_y, uint32_t w, uint32_t h)
{
	unsigned char* buffer = subimage_buffers[subbuf_index];
	vk::uploadPixelsToImage(
		buffer,
		begin_x, begin_y,
		w, h,
		NUM_CHANNELS *SIZE_PER_CHANNEL,
		window_surface->resource
	);
}

void Pathtracer::upload_tile(uint32_t subbuf_index, uint32_t tile_index) {
	uint32_t X = tile_index % tiles_X;
	uint32_t Y = tile_index / tiles_X;
	uint32_t tile_size = cached_config.TileSize;

	uint32_t tile_w = std::min(tile_size, width - X * tile_size);
	uint32_t tile_h = std::min(tile_size, height - Y * tile_size);

	uint32_t x_offset = X * tile_size;
	uint32_t y_offset = Y * tile_size;

	upload_tile(subbuf_index, x_offset, y_offset, tile_w, tile_h);
}
#else
void Pathtracer::raytrace_scene_to_buf() {

	if (cached_config.ISPC)
	{
		load_ispc_data();
		LOG("ispc max depth: %u", ispc_data->bvh_stack_size);

		// dispatch task to ispc
		ispc::raytrace_scene_ispc(
			ispc_data->camera.data(),
			(float*)ispc_data->pixel_offsets.data(),
			ispc_data->num_offsets,
			ispc_data->triangles.data(),
			ispc_data->bsdfs.data(),
			ispc_data->area_light_indices.data(),
			ispc_data->num_triangles,
			ispc_data->num_area_lights,
			image_buffer,
			ispc_data->width,
			ispc_data->height,
			false,
			ispc_data->tile_size,
			0, 0, // tile_width, tile_height
			0, 0, // tile_indexX, tile_indexY
			ispc_data->num_threads,
			ispc_data->max_ray_depth,
			ispc_data->rr_threshold,
			ispc_data->use_direct_light,
			ispc_data->area_light_samples,
			ispc_data->bvh_root.data(),
			ispc_data->bvh_stack_size,
			ispc_data->use_bvh,
			ispc_data->use_dof,
			ispc_data->focal_distance,
			ispc_data->aperture_radius);
	}
	else
	{
		if (cached_config.Multithreaded)
		{
			myn::ThreadSafeQueue<uint> tasks;
			uint task_size = cached_config.TileSize * cached_config.TileSize;
			uint image_size = width * height;
			for (uint i = 0; i < image_size; i += task_size) {
				tasks.enqueue(i);
			}
			std::function<void(int)> raytrace_task = [&](int tid){
				uint task_begin;
				while (tasks.dequeue(task_begin))
				{
					uint task_end = glm::min(image_size, task_begin + task_size);
					for (uint task = task_begin; task < task_end; task++)
					{
						vec3 color = raytrace_pixel(task);
						color = gamma_correct(color);
						set_mainbuffer_rgb(task, color);
					}
				}
			};
			TRACE("enqueued %zu tasks", tasks.size());
			// create the threads and execute
			std::vector<std::thread> threads_tmp;
			for (uint tid = 0; tid < cached_config.NumThreads; tid++) {
				threads_tmp.emplace_back(raytrace_task, tid);
			}
			TRACE("created %d threads", cached_config.NumThreads);
			for (uint tid = 0; tid < cached_config.NumThreads; tid++) {
				threads_tmp[tid].join();
			}
			TRACE("joined threads");
		}
		else
		{
			for (uint32_t y = 0; y < height; y++) {
				for (uint32_t x = 0; x < width; x++) {

					uint32_t px_index = width * y + x;
					vec3 color = raytrace_pixel(px_index);
					set_mainbuffer_rgb(px_index, color);

				}
			}
		}

	}

}

void Pathtracer::output_file(const std::string& path) {
	stbi_write_png(
		path.c_str(),
		width, height,4,
		image_buffer,
		width * 4);
}

// though render to file from GUI is not implemented yet...
void Pathtracer::render_to_file(const std::string& output_path_rel_to_bin)
{
#if GRAPHICS_DISPLAY
	if (!initialized) initialize();
#else
	initialize();
#endif

	double num_camera_rays = double(width * height * pixel_offsets.size()) * 1e-6;
	std::string workload = std::to_string((int)(num_camera_rays * 1000) * 0.001) + "M camera rays, "
		+ "max depth " + std::to_string(cached_config.MaxRayDepth) + ", "
		+ "RR threshold " + std::to_string((int)(cached_config.RussianRouletteThreshold * 100) * 0.01);

	uint32_t num_camera_rays_per_task = cached_config.TileSize * cached_config.TileSize * pixel_offsets.size();
	std::string threading = std::to_string(num_camera_rays_per_task) + " camera rays per tile, ";
	if (cached_config.Multithreaded) {
		threading += std::to_string(cached_config.NumThreads) + " threads";
	} else {
		threading += "single threaded";
	}

	TRACE("initialization complete. starting...\n\t%s\n\t%s", workload.c_str(), threading.c_str())
	TIMER_BEGIN
	raytrace_scene_to_buf();
	TIMER_END(duration)
	TRACE("done! took %f seconds", duration)

	output_file(output_path_rel_to_bin);
}
#endif
//
// Created by miyehn on 11/13/2022.
//

#include <stb_image/stb_image_write.h>
#include <windows.h>
#include "CpuTexture.h"
#include "Log.h"

namespace myn {

using namespace glm;

CpuTexture::CpuTexture(int width, int height) : width(width), height(height), buffer(width * height) {}

void CpuTexture::storeTexel(int x, int y, const glm::vec4 &col) {
	buffer[y * width + x] = col;
}

glm::vec4 CpuTexture::loadTexel(int x, int y) const {
	return buffer[y * width + x];
}

void CpuTexture::writeFile(const std::string &filename, bool gammaCorrect, bool openFile) const {
	std::vector<u8vec4> u8buf(buffer.size());
	for (int i = 0; i < buffer.size(); i++) {
		auto p = buffer[i];
		p = clamp(p, vec4(0), vec4(1));
		if (gammaCorrect) {
			const vec4 gamma(vec3(0.455f), 1.0f);
			p = glm::pow(p, gamma);
		}
		u8buf[i] = u8vec4(p.x * 255, p.y * 255, p.z * 255, p.w * 255);
	}
	stbi_write_png(filename.c_str(), width, height, 4, u8buf.data(), width * 4);
	if (openFile) {
		ShellExecute(0, "open", filename.c_str(), 0, 0, SW_SHOW);
	}
}

glm::vec4 CpuTexture::sampleBilinear(glm::vec2 uv, CpuTexture::WrapMode wm) const {
	if (wm == WM_Clamp) {
		uv = glm::clamp(uv, vec2(0), vec2(1));
	} else if (wm == WM_Wrap) {
		float usign = uv.x >= 0 ? 1 : -1;
		float vsign = uv.y >= 0 ? 1 : -1;
		uv = glm::fract(glm::abs(uv)) * vec2(usign, vsign);
	} else {
		ASSERT(false)
	}
	vec2 coords = vec2(width * uv.x, height * uv.y);
	vec2 deci = glm::fract(coords);
	ivec2 c00(
		min(width-1, int(glm::floor(coords.x))),
		min(height-1, int(glm::floor(coords.y))));
	ivec2 c10(min(width-1, c00.x + 1), c00.y);
	ivec2 c01(c00.x, min(height-1, c00.y + 1));
	ivec2 c11(min(width-1, c00.x + 1), min(height-1, c00.y + 1));
	vec4 v00 = loadTexel(c00.x, c00.y);
	vec4 v10 = loadTexel(c10.x, c10.y);
	vec4 v01 = loadTexel(c01.x, c01.y);
	vec4 v11 = loadTexel(c11.x, c11.y);
	vec4 y0 = v00 * (1.0f - deci.x) + v10 * deci.x;
	vec4 y1 = v01 * (1.0f - deci.x) + v11 * deci.x;
	return y0 * (1.0f - deci.y) + y1 * deci.y;
}


} // namespace myn


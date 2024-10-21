//
// Created by miyehn on 11/10/2022.
//

#include "Utils/myn/Log.h"
#include "CpuSkyAtmosphere/CpuSkyAtmosphere.h"
#include "Utils/myn/CpuTexture.h"
#include "Utils/myn/Timer.h"
#include <cxxopts/cxxopts.hpp>

int main(int argc, const char * argv[])
{
	cxxopts::Options options("vincent", "skybox");
	options.allow_unrecognised_options();
	options.add_options()
		("w,width", "window width", cxxopts::value<int>())
		("h,height", "window height", cxxopts::value<int>())
		("o,output", "output relative_path", cxxopts::value<std::string>());

	auto optargs = options.parse(argc, argv);

	if (!optargs.count("output") || !optargs.count("width") || !optargs.count("height")) {
		ERR("required arguments not set.")
		return 0;
	}

	int width = optargs["width"].as<int>();
	int height = optargs["height"].as<int>();
	std::string output_path = optargs["output"].as<std::string>();

	LOG("output path: %s", output_path.c_str())

	/////////////////////////////////////////////////////////////////////////////

	myn::sky::CpuSkyAtmosphere sky;
	sky.updateLuts();

	// create longlat map (main render pass)
	myn::CpuTexture skyTexture = sky.createSkyTexture(width, height);

	// query transmittance to sun
	glm::vec3 transmittanceToSun = sky.sampleSunTransmittance(sky.renderingParams.dir2sun);
	LOG("transmittance to sun: (%.3f, %.3f, %.3f)", transmittanceToSun.r, transmittanceToSun.g, transmittanceToSun.b);

	// output to file
	skyTexture.writeFile_R32G32B32(output_path, true);

	return 0;
}

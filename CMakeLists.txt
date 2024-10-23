cmake_minimum_required(VERSION 3.1)
project("niar")

set(CMAKE_CXX_STANDARD 20)
# set(CMAKE_CXX_FLAGS "-O3")

if(WIN32)
	set(VULKAN_SDK C:/VulkanSDK/1.3.296.0)
	set(Vulkan_LIBRARY ${VULKAN_SDK}/Lib/vulkan-1.lib)
	set(ISPC_CXX_COMPILER cl)
endif(WIN32)

if(APPLE)
	set(ISPC_CXX_COMPILER g++)
endif(APPLE)

find_package(Threads)
find_package(Vulkan REQUIRED)
message(STATUS "Vulkan Found = ${Vulkan_FOUND}")
message(STATUS "Vulkan Include = ${Vulkan_INCLUDE_DIR}")
message(STATUS "Vulkan Lib = ${Vulkan_LIBRARY}")

set(ELLYN_SRC
	src/Ellyn.cpp
	src/Render/Vulkan/Vulkan.cpp
	src/Scene/Scene.cpp
    src/Scene/Camera.cpp
	src/Scene/SceneObject.cpp
	src/Render/Mesh.cpp
    src/Scene/GrassField.cpp
	src/Assets/ConfigAsset.cpp
	src/Scene/Light.cpp
	src/Scene/AABB.cpp
	src/Pathtracer/Primitive.cpp
	src/Pathtracer/Pathtracer.cpp
	src/Pathtracer/PathtracerCore.cpp
	src/Pathtracer/PathtracerBufferOperations.cpp
	src/Pathtracer/BSDF.cpp
	src/Pathtracer/PathtracerLight.cpp
	src/Pathtracer/BVH.cpp
	# ${CMAKE_BINARY_DIR}/pathtracer_kernel.o # ISPC-specific
	${CMAKE_SOURCE_DIR}/include/imgui/imgui.h
	${CMAKE_SOURCE_DIR}/include/imgui/imgui.cpp
	${CMAKE_SOURCE_DIR}/include/imgui/imgui_demo.cpp
	${CMAKE_SOURCE_DIR}/include/imgui/imgui_tables.cpp
	${CMAKE_SOURCE_DIR}/include/imgui/imgui_draw.cpp
	${CMAKE_SOURCE_DIR}/include/imgui/backends/imgui_impl_sdl.cpp
	${CMAKE_SOURCE_DIR}/include/imgui/backends/imgui_impl_vulkan.cpp
	${CMAKE_SOURCE_DIR}/include/imgui/imgui_widgets.cpp
	src/Render/Vulkan/VulkanMemoryAllocatorImpl.cpp
	src/Utils/myn/Misc.cpp
    src/Render/Vulkan/PipelineBuilder.cpp
	src/Render/Vulkan/RenderPassBuilder.cpp
	src/Render/Vulkan/Buffer.cpp
	src/Render/Vulkan/DescriptorSet.cpp
	src/Render/Texture.cpp
	src/Utils/StbImageImpl.cpp
	src/Utils/TinyGLTFImpl.cpp
	src/Utils/myn/RenderDoc.cpp
	src/Render/Vulkan/SamplerCache.cpp
	src/Render/Vulkan/ShaderModule.cpp
	src/Render/Vulkan/VulkanUtils.cpp
	src/Render/Renderers/DeferredRenderer.cpp
	src/Render/Vulkan/ImageCreator.cpp
	src/Utils/DebugUI.cpp
	src/Render/Materials/GltfMaterial.cpp
	src/Render/DebugDraw.cpp
	src/Render/Renderers/SimpleRenderer.cpp
	src/Scene/PathtracerController.cpp
	src/Assets/Asset.cpp
	src/Assets/SceneAsset.cpp
	src/Render/Materials/GltfMaterialInfo.cpp
	src/Assets/EnvironmentMapAsset.cpp
	src/Utils/TinyExrImpl.cpp
	src/Utils/myn/Sample.cpp
	src/Scene/MeshObject.cpp
	src/Scene/Probe.cpp
	src/Scene/SkyAtmosphere/SkyAtmosphere.cpp
	src/Utils/myn/ShaderSimulator.cpp
	src/CpuSkyAtmosphere/CpuSkyAtmosphere.cpp
	src/Utils/myn/CpuTexture.cpp)

set(RTX_SRC
	src/Render/Vulkan/ShaderBindingTable.cpp
	src/Scene/RtxTriangle.cpp
	src/Render/Renderers/RayTracingRenderer.cpp
	src/Render/Materials/ComputeShader.cpp
	src/Scene/SkyAtmosphere/SkyAtmosphereShaders.cpp)

set(ASZELEA_SRC
	src/Aszelea.cpp
	src/Scene/SceneObject.cpp
	src/Scene/Camera.cpp
	src/Scene/Light.cpp
	src/Scene/AABB.cpp
	src/Render/Materials/GltfMaterialInfo.cpp
	src/Render/Mesh.cpp
	src/Scene/MeshObject.cpp
	src/Assets/Asset.cpp
	src/Assets/SceneAsset.cpp
	src/Assets/ConfigAsset.cpp
	src/Pathtracer/Primitive.cpp
	src/Pathtracer/BSDF.cpp
	src/Pathtracer/PathtracerLight.cpp
	src/Pathtracer/BVH.cpp
	src/Pathtracer/Pathtracer.cpp
	src/Pathtracer/PathtracerCore.cpp
	src/Pathtracer/PathtracerBufferOperations.cpp
	src/Utils/myn/Misc.cpp
	src/Utils/TinyGLTFImpl.cpp
	src/Utils/StbImageImpl.cpp
	# ${CMAKE_BINARY_DIR}/pathtracer_kernel.o # ISPC-specific
	src/Assets/EnvironmentMapAsset.cpp
	src/Utils/TinyExrImpl.cpp
	src/Utils/myn/Sample.cpp
	src/Scene/SkyAtmosphere/SkyAtmosphere.cpp
	src/Utils/myn/ShaderSimulator.cpp
	src/CpuSkyAtmosphere/CpuSkyAtmosphere.cpp
	src/Utils/myn/CpuTexture.cpp)

set(VINCENT_SRC
	src/Vincent.cpp
	src/Utils/StbImageImpl.cpp
	src/Utils/TinyExrImpl.cpp
	src/Utils/myn/ShaderSimulator.cpp
	src/CpuSkyAtmosphere/CpuSkyAtmosphere.cpp
	src/Utils/myn/CpuTexture.cpp)

if(APPLE)
	add_definitions(-DMACOS)

	add_executable(ellyn ${ELLYN_SRC})
	target_link_libraries(ellyn
		${CMAKE_THREAD_LIBS_INIT}
		${CMAKE_SOURCE_DIR}/lib/libMoltenVK.dylib
		${CMAKE_SOURCE_DIR}/lib/libGLEW.a 
		${CMAKE_SOURCE_DIR}/lib/SDL2.framework 
		${CMAKE_SOURCE_DIR}/lib/libconfig++.a
	)

	add_executable(asz ${ASZELEA_SRC})
	target_link_libraries(asz ${CMAKE_SOURCE_DIR}/lib/libconfig++.a)

	add_executable(vin ${VINCENT_SRC})
	target_link_libraries(asz ${CMAKE_SOURCE_DIR}/lib/libconfig++.a)
endif(APPLE)

if(WIN32)
	add_definitions(-DWINOS)

	#if

	add_executable(ellyn WIN32 ${ELLYN_SRC} ${RTX_SRC})
	target_link_libraries(ellyn
		${CMAKE_THREAD_LIBS_INIT}
		${Vulkan_LIBRARY}
		${CMAKE_SOURCE_DIR}/lib/glew32s.lib 
		${CMAKE_SOURCE_DIR}/lib/SDL2x64.lib
		${CMAKE_SOURCE_DIR}/lib/libconfig++d.lib
	)
	set_target_properties(ellyn PROPERTIES LINK_FLAGS /SUBSYSTEM:CONSOLE)

	add_executable(asz WIN32 ${ASZELEA_SRC})
	target_link_libraries(asz ${CMAKE_SOURCE_DIR}/lib/libconfig++d.lib)
	set_target_properties(asz PROPERTIES LINK_FLAGS /SUBSYSTEM:CONSOLE)

	add_executable(vin ${VINCENT_SRC})
	target_link_libraries(vin ${CMAKE_SOURCE_DIR}/lib/libconfig++d.lib)
	set_target_properties(vin PROPERTIES LINK_FLAGS /SUBSYSTEM:CONSOLE)

	configure_file(lib/SDL2.dll ${ellyn_BINARY_DIR} SDL2.dll COPYONLY)
	configure_file(lib/libconfig++d.dll ${ellyn_BINARY_DIR} libconfig++d.dll COPYONLY)
endif(WIN32)

# include_directories(include src)
include_directories(PUBLIC src include)
target_include_directories(ellyn PUBLIC ${Vulkan_INCLUDE_DIR})
target_include_directories(ellyn PUBLIC include/SDL2)
target_include_directories(ellyn PUBLIC include/imgui)
target_include_directories(vin PUBLIC include)

target_compile_definitions(ellyn PRIVATE GRAPHICS_DISPLAY=1)
target_compile_definitions(asz PRIVATE GRAPHICS_DISPLAY=0)
target_compile_definitions(vin PRIVATE GRAPHICS_DISPLAY=0)

# definitions
add_definitions(-DROOT_DIR="${CMAKE_SOURCE_DIR}")
add_definitions(-DCMAKE_EXPORT_COMPILE_COMMANDS=ON)

add_definitions(-DDEBUG=1)
add_definitions(-DISPC=0)

message(STATUS "${CMAKE_SOURCE_DIR}/lib/libconfig++d.lib")

# pathtracer_kernel.o
set(KERNEL_SRC ${CMAKE_SOURCE_DIR}/src/Pathtracer/pathtracer_kernel.ispc)
set(KERNEL_INCLUDE ${CMAKE_SOURCE_DIR}/src/Pathtracer/pathtracer_kernel_utilities.h)
set(KERNEL_H ${CMAKE_SOURCE_DIR}/src/Pathtracer/pathtracer_kernel_ispc.h)
set(KERNEL_O ${CMAKE_BINARY_DIR}/pathtracer_kernel.o)
add_custom_command(
	OUTPUT ${KERNEL_H} ${KERNEL_O}
	COMMAND ispc -O2 --target=avx2-i32x8 --arch=x86-64 ${KERNEL_SRC} -h ${KERNEL_H} -o ${KERNEL_O}
	DEPENDS ${KERNEL_SRC}#${KERNEL_INCLUDE}
	COMMENT "generating pathtracer_kernel.o"
)
# build
add_custom_target(ispc
	DEPENDS ${KERNEL_H} ${KERNEL_O}
	WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

# [10/2024] disabling ispc for now because it's not installed on this pc
# add_dependencies(ellyn ispc)

#-------- vulkan shaders --------
set(VULKAN_SHADERS_SRC_DIR ${CMAKE_SOURCE_DIR}/shaders)
set(VULKAN_SHADERS_BIN_DIR ${CMAKE_BINARY_DIR}/spirv)
add_custom_target(shaders ALL
	COMMENT "compiling Vulkan shaders"
	COMMAND ${CMAKE_SOURCE_DIR}/scripts/compile_vulkan_shaders.sh ${VULKAN_SHADERS_SRC_DIR} ${VULKAN_SHADERS_BIN_DIR}
)
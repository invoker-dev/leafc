cmake_minimum_required(VERSION 3.27)

project(leafc LANGUAGES C CXX)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE debug)
endif()

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(CMAKE_BUILD_TYPE STREQUAL "debug")
  add_compile_options(
    -g3
    -O0
    -fno-inline # no inlining functions

    -Wall
    -Wpedantic
    -Wextra
  )
endif()

if(CMAKE_BUILD_TYPE STREQUAL "release")
  add_compile_options(
    -O3
    -DNDEBUG
  )
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# -----------------------------------------------------------------------------
# dependecies
# -----------------------------------------------------------------------------
include(FetchContent)

FetchContent_Declare(
  volk # loads vulkan dynamically
  GIT_REPOSITORY https://github.com/zeux/volk
  GIT_TAG master
)

FetchContent_Declare(
  cglm
  GIT_REPOSITORY https://github.com/recp/cglm
  GIT_TAG master
)

FetchContent_Declare(
  stb
  GIT_REPOSITORY https://github.com/nothings/stb
  GIT_TAG master
)

FetchContent_Declare(
  vma
  GIT_REPOSITORY https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator
  GIT_TAG master
)

FetchContent_Declare(
  cgltf
  GIT_REPOSITORY https://github.com/jkuhlmann/cgltf
  GIT_TAG master
)

FetchContent_MakeAvailable(
  volk
  cglm
  stb
  vma
  cgltf
)

find_package(Vulkan REQUIRED COMPONENTS SPIRV-Tools)
find_package(VulkanUtilityLibraries REQUIRED)
find_package(SDL3 REQUIRED)

file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS "src/*.c")
list(APPEND SOURCES "src/vma_impl.cpp")

add_executable(${PROJECT_NAME} ${SOURCES})

add_library(stb INTERFACE)
target_include_directories(stb INTERFACE ${stb_SOURCE_DIR})

add_library(cgltf INTERFACE)
target_include_directories(cgltf INTERFACE ${cgltf_SOURCE_DIR})

target_compile_definitions(${PROJECT_NAME} PRIVATE VK_NO_PROTOTYPES)

target_link_libraries(${PROJECT_NAME} PRIVATE
  volk
  SDL3::SDL3
  cglm
  stb
  cgltf
  Vulkan::Headers
  Vulkan::UtilityHeaders
  Vulkan::SPIRV-Tools
  GPUOpen::VulkanMemoryAllocator
  # Vulkan::LayerSettings
)

cmake_minimum_required(VERSION 4.0)
# set(CMAKE_EXPERIMENTAL_CXX_IMPORT_STD "d0edc3af-4c50-42ea-a356-e2862fe7a444")
# set(CMAKE_CXX_MODULE_STD ON)
project(vorn VERSION 1.0 LANGUAGES CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_SCAN_FOR_MODULES ON)
set(CMAKE_CXX_FLAGS "-Wall -Werror -Wextra")

find_package(Vulkan REQUIRED)
include(FetchContent)
FetchContent_Declare(
       VulkanMemoryAllocator
       GIT_REPOSITORY GIT_REPOSITORY https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator.git
       GIT_TAG v3.3.0
)
FetchContent_MakeAvailable(VulkanMemoryAllocator)

# set up Vulkan C++ module (enabled when ENABLE_CPP20_MODULE=ON)
add_library(VulkanCppModule)
add_library(Vulkan::cppm ALIAS VulkanCppModule)
target_compile_definitions(VulkanCppModule PUBLIC
       VULKAN_HPP_DISPATCH_LOADER_DYNAMIC=1
       VULKAN_HPP_NO_STRUCT_CONSTRUCTORS=1
       VULKAN_HPP_NO_EXCEPTIONS=1
)
target_include_directories(VulkanCppModule PRIVATE "${Vulkan_INCLUDE_DIR}")
target_link_libraries(VulkanCppModule PUBLIC Vulkan::Vulkan)
set_target_properties(VulkanCppModule PROPERTIES CXX_STANDARD 23)
target_sources(VulkanCppModule
       PUBLIC
       FILE_SET cxx_modules TYPE CXX_MODULES
       BASE_DIRS "${Vulkan_INCLUDE_DIR}"
       FILES "${Vulkan_INCLUDE_DIR}/vulkan/vulkan.cppm"
)

if(DEFINED ENV{SLANGC_EXECUTABLE})
    set(SLANGC_EXECUTABLE $ENV{SLANGC_EXECUTABLE})
else()
       message(FATAL_ERROR "slangc not found. Must set SLANGC_EXECUTABLE env variable")
endif()
set(SHADER_SLANG_SOURCES ${CMAKE_CURRENT_LIST_DIR}/shaders/shader.slang)
# slang compile function
function (add_slang_shader_target TARGET)
       cmake_parse_arguments ("SHADER" "" "" "SOURCES" ${ARGN})
       set (SHADERS_DIR ${CMAKE_CURRENT_LIST_DIR}/shaders)
       set (ENTRY_POINTS -entry vertMain -entry fragMain)
       add_custom_command (
              OUTPUT ${SHADERS_DIR}
              COMMAND ${CMAKE_COMMAND} -E make_directory ${SHADERS_DIR}
       )
       add_custom_command (
              OUTPUT  ${SHADERS_DIR}/slang.spv
              COMMAND ${SLANGC_EXECUTABLE} ${SHADER_SOURCES} -target spirv -profile spirv_1_4 -emit-spirv-directly -fvk-use-entrypoint-name ${ENTRY_POINTS} -o shader.spv
              WORKING_DIRECTORY ${SHADERS_DIR}
              DEPENDS ${SHADERS_DIR} ${SHADER_SOURCES}
              COMMENT "Compiling Slang Shaders"
              VERBATIM
       )
       add_custom_target (${TARGET} DEPENDS ${SHADERS_DIR}/slang.spv)
endfunction()
add_slang_shader_target(shaders SOURCES ${SHADER_SLANG_SOURCES})


find_package(glm REQUIRED)
find_package(glfw3 REQUIRED)

file(GLOB_RECURSE SRCS "src/*.cpp")
file(GLOB_RECURSE MODULES "src/*.cppm")

add_executable(${PROJECT_NAME})
add_dependencies(${PROJECT_NAME} shaders)
target_sources(${PROJECT_NAME}
  PRIVATE ${SRCS}
  PRIVATE FILE_SET cxx_modules TYPE CXX_MODULES FILES ${MODULES}
)
# set_target_properties(${PROJECT_NAME} PROPERTIES CXX_MODULE_STD 1)
target_link_libraries(${PROJECT_NAME} PRIVATE
       Vulkan::cppm
       glm::glm
       glfw
       GPUOpen::VulkanMemoryAllocator
)

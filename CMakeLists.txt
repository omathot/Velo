cmake_minimum_required(VERSION 4.0)
set(CMAKE_EXPERIMENTAL_CXX_IMPORT_STD "d0edc3af-4c50-42ea-a356-e2862fe7a444")
set(CMAKE_CXX_MODULE_STD ON)
project(velo VERSION 1.0 LANGUAGES CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_SCAN_FOR_MODULES ON)

find_package(Vulkan REQUIRED)
include(FetchContent)
FetchContent_Declare(
       VulkanMemoryAllocator
       GIT_REPOSITORY https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator.git
       GIT_TAG v3.3.0
       SYSTEM
)
FetchContent_MakeAvailable(VulkanMemoryAllocator)

FetchContent_Declare(
       tinyobjloader
       GIT_REPOSITORY https://github.com/tinyobjloader/tinyobjloader.git
       GIT_TAG        origin/release
       SYSTEM
)
FetchContent_MakeAvailable(tinyobjloader)

FetchContent_Declare(
       imgui
       GIT_REPOSITORY https://github.com/ocornut/imgui.git
       GIT_TAG v1.92.5
)
FetchContent_MakeAvailable(imgui)
# manually create imgui lib, no cmake
add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    # backend for GLFW + Vulkan
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp
)
target_include_directories(imgui PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)
target_link_libraries(imgui PUBLIC glfw Vulkan::Vulkan)
add_library(imgui::imgui ALIAS imgui)

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
       # very long -profile is to explicitly enable slangc features
       # not necessary slangc turns them on automatically when needed
       add_custom_command (
              OUTPUT  ${SHADERS_DIR}/slang.spv
              COMMAND ${SLANGC_EXECUTABLE} ${SHADER_SOURCES}
                     -target spirv
                     -profile spirv_1_6+SPV_GOOGLE_user_type+spvDerivativeControl+spvImageQuery+spvImageGatherExtended+spvSparseResidency+spvMinLod+spvFragmentFullyCoveredEXT
                     -emit-spirv-directly
                     -fvk-use-entrypoint-name ${ENTRY_POINTS}
                     -o shader.spv
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

option(CODAM "Enable CODAM logic" OFF)
option(X11 "Enabled X11 logic" OFF)
option(INFOS "Fetching infos" OFF)

if(CODAM)
       message(STATUS "Enabled Codam")
       target_compile_definitions(${PROJECT_NAME} PRIVATE CODAM)
endif()
if (X11)
       message(STATUS "Enabled X11")
       target_compile_definitions(${PROJECT_NAME} PRIVATE X11)
endif()
if (INFOS)
       message(STATUS "Enabled Infos")
       target_compile_definitions(${PROJECT_NAME} PRIVATE INFOS)
endif()

add_dependencies(${PROJECT_NAME} shaders)
target_sources(${PROJECT_NAME}
  PRIVATE ${SRCS}
  PRIVATE FILE_SET cxx_modules TYPE CXX_MODULES FILES ${MODULES}
)

set(SANITIZER_FLAGS -fsanitize=address -fsanitize=undefined)
target_compile_options(${PROJECT_NAME} PRIVATE
       -Wall -Werror -Wextra -Wshadow -Wconversion --coverage ${SANITIZER_FLAGS}
)
target_link_options(${PROJECT_NAME} PRIVATE
       --coverage ${SANITIZER_FLAGS}
)

set_target_properties(${PROJECT_NAME} PROPERTIES CXX_MODULE_STD 1)
target_link_libraries(${PROJECT_NAME} PRIVATE
       Vulkan::cppm
       glm::glm
       glfw
       GPUOpen::VulkanMemoryAllocator
       imgui::imgui
       tinyobjloader
)

# clang-tidy
option(TIDY "Enable clang-tidy pass" OFF)
if (TIDY)
       find_program(CLANG_TIDY_EXE NAMES clang-tidy)
       if(CLANG_TIDY_EXE)
              set_target_properties(${PROJECT_NAME} PROPERTIES
                     CXX_CLANG_TIDY "${CLANG_TIDY_EXE}"
              )
              message(STATUS "clang-tidy enabled: Expect longer compile times")
       else()
              message(FATAL_ERROR "clang-tidy not found")
       endif()
endif()

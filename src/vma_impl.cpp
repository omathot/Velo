// cam be more surgical about it later for now just nuke warnings
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"

#define VMA_IMPLEMENTATION
#define VMA_STATIC_VULKAN_FUNCTIONS 0
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 1
#include <vk_mem_alloc.h>

#pragma clang diagnostic pop

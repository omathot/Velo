# Velo

A project to teach myself more about GPUs, modern Vulkan practices and C++23.

Velo uses [dynamic rendering](https://docs.vulkan.org/samples/latest/samples/extensions/dynamic_rendering/README.html), [timeline semaphores](https://docs.vulkan.org/samples/latest/samples/extensions/timeline_semaphore/README.html), as well as
[descriptor indexing](https://docs.vulkan.org/samples/latest/samples/extensions/descriptor_indexing/README.html) and the associated features needed for bindless descriptors.
Memory is allocated using [Vulkan Memory Allocator](https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator).

## Requirements

- Cmake 4.x [(downloads page)](https://cmake.org/download/)
- Ninja
- Vulkan 1.3
- Slang [(github releases)](https://github.com/shader-slang/slang/releases)
  - **Important** make sure you have *SLANGC_EXECUTABLE* environment variable set and pointing to the slangc executable itself. Vulkan SDK ships with it included.
    - bash
      ```
      export SLANGC_EXECUTABLE="$VULKAN_SDK/bin/slangc"
      or
      export SLANGC_EXECUTABLE=~/path/to/slangc
      ```
    - fish
      ```
      set -Ux SLANGC_EXECUTABLE $VULKAN_SDK/bin/slangc
      or
      set -Ux SLANGC_EXECUTABLE /path/to/slangc
      ```
- Clang
- glm
- glfw

## Build

```
git clone https://github.com/omathot/Velo.git
cd Velo
cmake -G Ninja -B build -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_BUILD_TYPE=Debug
cmake --build build

./build/velo
```

### Options
```
-DX11=ON (force X11 - useful for renderdoc)
-DINFOS=ON (creates infos/ dir and stores available vk features/extensions/layers and required glfw extensions)
-DTIDY=ON (run clang-tidy on Velo, longer build times)
-DCODAM=ON (different code path, testing repurposing this for a codam advanced project)
```

## Controls
- W : Move object further
- S : Move object closer
- A : Move object to the left
- D : Move object to the right
- UP : Move object up
- DOWN : Move object down
- SPACE : Switch rotation direction
- C : Start/Stop rotation toggle
- \- : Slow rotation down
- = : Speed rotation up

## Screenshots
![image](https://github.com/user-attachments/assets/eece64d3-cf94-4062-a82c-0b239c4a457e)

![image](https://github.com/user-attachments/assets/8a3e72d8-ba49-4663-896a-7e6a445e8f46)

Tested on 5070 and 9070XT

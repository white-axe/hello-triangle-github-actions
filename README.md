# hello-triangle-github-actions

This repository contains example GitHub Actions workflows demonstrating usage of Direct3D 11, OpenGL, Metal and Vulkan in the GitHub-hosted GitHub Actions runners. This can be a bit challenging due to the fact that the Linux and Windows runners don't have a graphics card. For these example workflows, the graphics APIs are all backed by software renderers, except for Metal which is backed by an actual graphics card:

| | **Linux** | **macOS** | **Windows** |
|-|-|-|-|
| **Direct3D 11** | | | [WARP](https://learn.microsoft.com/en-us/windows/win32/direct3darticles/directx-warp) |
| **Metal** | | GPU | |
| **OpenGL** | [Mesa LLVMpipe](https://docs.mesa3d.org/drivers/llvmpipe.html) | `kCGLRendererAppleSWID` | [Mesa LLVMpipe](https://docs.mesa3d.org/drivers/llvmpipe.html) |
| **Vulkan** | [Mesa LLVMpipe](https://docs.mesa3d.org/drivers/llvmpipe.html) | [Mesa LLVMpipe](https://docs.mesa3d.org/drivers/llvmpipe.html) | [Mesa LLVMpipe](https://docs.mesa3d.org/drivers/llvmpipe.html) |

The directories direct3d11, metal, opengl and vulkan each contain a basic "Hello Triangle" application that renders the following tricolored triangle to a PNG image file using their respective rendering APIs.

![Hello Triangle](triangle-reference.png)

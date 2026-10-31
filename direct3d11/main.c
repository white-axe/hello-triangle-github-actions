#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi.h>

#define GLFW_INCLUDE_NONE /* prevents GLFW from including OpenGL headers */
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#define WIDTH 640
#define HEIGHT 480

static GLFWwindow *window = NULL;
static HWND win32_window = NULL;
static IDXGIFactory *factory = NULL;
static ID3D11Device *device = NULL;
static IDXGIAdapter *adapter = NULL;
static ID3D11DeviceContext *device_context = NULL;
static IDXGISwapChain *swapchain = NULL;
static ID3D11Texture2D *back_buffer = NULL;
static ID3D11RenderTargetView *render_target_view = NULL;
static ID3DBlob *vs_blob = NULL;
static ID3D11VertexShader *vs = NULL;
static ID3DBlob *ps_blob = NULL;
static ID3D11PixelShader *ps = NULL;
static ID3D11InputLayout *vertex_buffer_layout = NULL;
static ID3D11Buffer *vertex_buffer = NULL;
static ID3D11Texture2D *staging_texture = NULL;

static void finalize(void) {
    if (staging_texture != NULL) {
        staging_texture->lpVtbl->Release(staging_texture);
    }
    if (render_target_view != NULL) {
        render_target_view->lpVtbl->Release(render_target_view);
    }
    if (back_buffer != NULL) {
        back_buffer->lpVtbl->Release(back_buffer);
    }
    if (swapchain != NULL) {
        swapchain->lpVtbl->Release(swapchain);
    }
    if (factory != NULL) {
        factory->lpVtbl->Release(factory);
    }
    if (vertex_buffer != NULL) {
        vertex_buffer->lpVtbl->Release(vertex_buffer);
    }
    if (vertex_buffer_layout != NULL) {
        vertex_buffer_layout->lpVtbl->Release(vertex_buffer_layout);
    }
    if (ps != NULL) {
        ps->lpVtbl->Release(ps);
    }
    if (ps_blob != NULL) {
        ps_blob->lpVtbl->Release(ps_blob);
    }
    if (vs != NULL) {
        vs->lpVtbl->Release(vs);
    }
    if (vs_blob != NULL) {
        vs_blob->lpVtbl->Release(vs_blob);
    }
    if (adapter != NULL) {
        adapter->lpVtbl->Release(adapter);
    }
    if (device_context != NULL) {
        device_context->lpVtbl->Release(device_context);
    }
    if (device != NULL) {
        device->lpVtbl->Release(device);
    }
    if (window != NULL) {
        glfwDestroyWindow(window);
    }
    glfwTerminate();
}

static void fatal_error(const char *message) {
    const char *error_description;
    int error_code = glfwGetError(&error_description);
    if (error_code == GLFW_NO_ERROR) {
        fprintf(stderr, "%s\n", message);
    } else {
        fprintf(stderr, "%s: 0x%08x (%s)\n", message, error_code, error_description);
    }
    finalize();
    exit(1);
}

static void init_window(void) {
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

    window = glfwCreateWindow(WIDTH, HEIGHT, "triangle-direct3d11", NULL, NULL);
    if (window == NULL) {
        fatal_error("Failed to create window");
    }

    win32_window = glfwGetWin32Window(window);
    if (win32_window == NULL) {
        fatal_error("Failed to get Win32 window");
    }
}

static void init_device(void) {
    const D3D_FEATURE_LEVEL feature_level = D3D_FEATURE_LEVEL_11_0;
    if (D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_WARP, NULL, 0, &feature_level, 1, D3D11_SDK_VERSION, &device, NULL, &device_context) != S_OK) {
        fatal_error("Failed to create D3D11 device");
    }

    IDXGIDevice *dxgi_device;
    device->lpVtbl->QueryInterface(device, &IID_IDXGIDevice, (void **)&dxgi_device);
    if (dxgi_device->lpVtbl->GetAdapter(dxgi_device, &adapter) != S_OK) {
        fatal_error("Failed to get DXGI adapter");
    }

    DXGI_ADAPTER_DESC adapter_desc;
    if (adapter->lpVtbl->GetDesc(adapter, &adapter_desc) != S_OK) {
        fatal_error("Failed to get DXGI adapter description");
    }

    adapter->lpVtbl->Release(adapter);
    adapter = NULL;

    printf("Vendor ID: 0x%04llx\n", (unsigned long long)adapter_desc.VendorId);
    printf("Device ID: 0x%04llx\n", (unsigned long long)adapter_desc.DeviceId);
    printf("Device name: %ls\n", adapter_desc.Description);
}

static ID3DBlob *compile_shader(const char *target, const char *filename, const char *shader_source_code) {
    ID3DBlob *shader, *error;
    if (D3DCompile(shader_source_code, strlen(shader_source_code), filename, NULL, NULL, "main", target, D3DCOMPILE_ENABLE_STRICTNESS, 0, &shader, &error) != S_OK) {
        fputs("Failed to compile shader\n", stderr);
        fatal_error((const char *)error->lpVtbl->GetBufferPointer(error));
    }
    return shader;
}

static void init_shaders(void) {
    vs_blob = compile_shader("vs_5_0", "shader.vs.hlsl", (
        "struct VS_INPUT {\n"
        "    float2 a_position: POSITION;\n"
        "    float3 a_color: COLOR0;\n"
        "};\n"
        "struct VS_OUTPUT {\n"
        "    float4 o_position: SV_Position;\n"
        "    float3 v_color: COLOR0;\n"
        "};\n"
        "VS_OUTPUT main(VS_INPUT input) {\n"
        "    VS_OUTPUT output;\n"
        "    output.o_position = float4(input.a_position, 0., 1.);\n"
        "    output.v_color = input.a_color;\n"
        "    return output;\n"
        "}\n"
    ));
    if (device->lpVtbl->CreateVertexShader(device, vs_blob->lpVtbl->GetBufferPointer(vs_blob), vs_blob->lpVtbl->GetBufferSize(vs_blob), NULL, &vs)) {
        fatal_error("Failed to create vertex shader");
    }
    device_context->lpVtbl->VSSetShader(device_context, vs, NULL, 0);

    ps_blob = compile_shader("ps_5_0", "shader.ps.hlsl", (
        "struct PS_INPUT {\n"
        "    float4 o_position: SV_Position;\n"
        "    float3 v_color: COLOR0;\n"
        "};\n"
        "struct PS_OUTPUT {\n"
        "    float4 o_color: SV_Target0;\n"
        "};\n"
        "PS_OUTPUT main(PS_INPUT input) {\n"
        "    PS_OUTPUT output;\n"
        "    output.o_color = float4(input.v_color.bgr, 1.); // need to swizzle the color channels since we're rendering to an image with BGRA pixel format instead of RGBA\n"
        "    return output;\n"
        "}\n"
    ));
    if (device->lpVtbl->CreatePixelShader(device, ps_blob->lpVtbl->GetBufferPointer(ps_blob), ps_blob->lpVtbl->GetBufferSize(ps_blob), NULL, &ps)) {
        fatal_error("Failed to create pixel shader");
    }
    device_context->lpVtbl->PSSetShader(device_context, ps, NULL, 0);
}

static void init_vertex_buffer(void) {
    static const D3D11_INPUT_ELEMENT_DESC input_element_desc[] = {
        { /* a_position */
            .SemanticName = "POSITION",
            .SemanticIndex = 0,
            .Format = DXGI_FORMAT_R32G32_FLOAT,
            .InputSlot = 0,
            .AlignedByteOffset = 0,
            .InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA,
            .InstanceDataStepRate = 0,
        },
        { /* a_color */
            .SemanticName = "COLOR",
            .SemanticIndex = 0,
            .Format = DXGI_FORMAT_R32G32B32_FLOAT,
            .InputSlot = 0,
            .AlignedByteOffset = 2 * sizeof(float),
            .InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA,
            .InstanceDataStepRate = 0,
        },
    };
    if (device->lpVtbl->CreateInputLayout(device, input_element_desc, sizeof input_element_desc / sizeof *input_element_desc, vs_blob->lpVtbl->GetBufferPointer(vs_blob), vs_blob->lpVtbl->GetBufferSize(vs_blob), &vertex_buffer_layout) != S_OK) {
        fatal_error("Failed to create vertex buffer layout");
    }
    device_context->lpVtbl->IASetInputLayout(device_context, vertex_buffer_layout);

    static const float vertex_attributes[] = {
        /* a_position */ /* a_color */
         0.0f,  0.5f,    1.0f, 0.0f, 0.0f,
         0.5f, -0.5f,    0.0f, 1.0f, 0.0f,
        -0.5f, -0.5f,    0.0f, 0.0f, 1.0f,
    };
    static const D3D11_BUFFER_DESC buffer_desc = {
        .ByteWidth = sizeof vertex_attributes,
        .Usage = D3D11_USAGE_DEFAULT,
        .BindFlags = D3D11_BIND_VERTEX_BUFFER,
        .CPUAccessFlags = 0,
        .MiscFlags = 0,
        .StructureByteStride = 0,
    };
    static const D3D11_SUBRESOURCE_DATA subresource_data = {
        .pSysMem = vertex_attributes,
        .SysMemPitch = 0,
        .SysMemSlicePitch = 0,
    };
    if (device->lpVtbl->CreateBuffer(device, &buffer_desc, &subresource_data, &vertex_buffer) != S_OK) {
        fatal_error("Failed to create vertex buffer");
    }

    UINT stride = 5 * sizeof(float);
    UINT offset = 0;
    device_context->lpVtbl->IASetVertexBuffers(device_context, 0, 1, &vertex_buffer, &stride, &offset);
    device_context->lpVtbl->IASetPrimitiveTopology(device_context, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
}

static void init_swapchain(void) {
    if (CreateDXGIFactory(&IID_IDXGIFactory, (void **)&factory) != S_OK) {
        fatal_error("Failed to create DXGI factory");
    }

    DXGI_SWAP_CHAIN_DESC swap_chain_desc = {
        .BufferDesc = {
            .Width = WIDTH,
            .Height = HEIGHT,
            .RefreshRate = {
                .Numerator = 60,
                .Denominator = 1,
            },
            .Format = DXGI_FORMAT_B8G8R8A8_UNORM,
            .ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_PROGRESSIVE,
            .Scaling = DXGI_MODE_SCALING_UNSPECIFIED,
        },
        .SampleDesc = {
            .Count = 1,
            .Quality = 0,
        },
        .BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT,
        .BufferCount = 2,
        .OutputWindow = win32_window,
        .Windowed = TRUE,
        .SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD,
        .Flags = 0,
    };
    IUnknown *device_unknown;
    device->lpVtbl->QueryInterface(device, &IID_IUnknown, (void **)&device_unknown);
    if (factory->lpVtbl->CreateSwapChain(factory, device_unknown, &swap_chain_desc, &swapchain) != S_OK) {
        fatal_error("Failed to create swapchain");
    }

    if (swapchain->lpVtbl->GetBuffer(swapchain, 0, &IID_ID3D11Texture2D, (void **)&back_buffer) != S_OK) {
        fatal_error("Failed to get back buffer");
    }

    ID3D11Resource *back_buffer_resource;
    back_buffer->lpVtbl->QueryInterface(back_buffer, &IID_ID3D11Resource, (void **)&back_buffer_resource);
    if (device->lpVtbl->CreateRenderTargetView(device, back_buffer_resource, NULL, &render_target_view) != S_OK) {
        fatal_error("Failed to create render target view");
    }
    device_context->lpVtbl->OMSetRenderTargets(device_context, 1, &render_target_view, NULL);

    static const D3D11_VIEWPORT viewport = {
        .TopLeftX = 0.0f,
        .TopLeftY = 0.0f,
        .Width = (float)WIDTH,
        .Height = (float)HEIGHT,
        .MinDepth = 0.0f,
        .MaxDepth = 1.0f,
    };
    device_context->lpVtbl->RSSetViewports(device_context, 1, &viewport);
}

static void save_back_buffer_to_disk(void) {
    D3D11_TEXTURE2D_DESC back_buffer_desc;
    back_buffer->lpVtbl->GetDesc(back_buffer, &back_buffer_desc);
    const D3D11_TEXTURE2D_DESC staging_texture_desc = {
        .Width = WIDTH,
        .Height = HEIGHT,
        .MipLevels = back_buffer_desc.MipLevels,
        .ArraySize = back_buffer_desc.ArraySize,
        .Format = back_buffer_desc.Format,
        .SampleDesc = {
            .Count = back_buffer_desc.SampleDesc.Count,
            .Quality = back_buffer_desc.SampleDesc.Quality,
        },
        .Usage = D3D11_USAGE_STAGING,
        .BindFlags = 0,
        .CPUAccessFlags = D3D11_CPU_ACCESS_READ,
        .MiscFlags = 0,
    };
    if (device->lpVtbl->CreateTexture2D(device, &staging_texture_desc, NULL, &staging_texture) != S_OK) {
        fatal_error("Failed to create staging texture");
    }

    ID3D11Resource *staging_texture_resource;
    staging_texture->lpVtbl->QueryInterface(staging_texture, &IID_ID3D11Resource, (void **)&staging_texture_resource);
    ID3D11Resource *back_buffer_resource;
    back_buffer->lpVtbl->QueryInterface(back_buffer, &IID_ID3D11Resource, (void **)&back_buffer_resource);
    device_context->lpVtbl->CopyResource(device_context, staging_texture_resource, back_buffer_resource);

    D3D11_MAPPED_SUBRESOURCE staging_texture_mapped;
    if (device_context->lpVtbl->Map(device_context, staging_texture_resource, 0, D3D11_MAP_READ, 0, &staging_texture_mapped) != S_OK) {
        fatal_error("Failed to map staging texture");
    }
    void *buffer;
    if (staging_texture_mapped.RowPitch != (size_t)4 * (size_t)WIDTH) {
        buffer = malloc((size_t)staging_texture_mapped.RowPitch * (size_t)HEIGHT);
        if (buffer == NULL) {
            device_context->lpVtbl->Unmap(device_context, staging_texture_resource, 0);
            fatal_error("Failed to allocate buffer");
        }
        for (size_t i = 0; i < (size_t)HEIGHT; ++i) {
            memcpy((uint8_t *)buffer + (size_t)4 * (size_t)WIDTH * i, (const uint8_t *)staging_texture_mapped.pData + (size_t)staging_texture_mapped.RowPitch * i, (size_t)4 * (size_t)WIDTH);
        }
    } else {
        buffer = staging_texture_mapped.pData;
    }
    puts("Writing screenshot to ./triangle.png");
    int result = stbi_write_png("triangle.png", WIDTH, HEIGHT, 4, staging_texture_mapped.pData, 0);
    if (staging_texture_mapped.RowPitch != (size_t)4 * (size_t)WIDTH) {
        free(buffer);
    }
    device_context->lpVtbl->Unmap(device_context, staging_texture_resource, 0);
    if (result == 0) {
        fatal_error("Failed to write to output file");
    }

    staging_texture->lpVtbl->Release(staging_texture);
    staging_texture = NULL;
}

static void draw_frame(void) {
    static const float clear_color[] = {0.0f, 0.0f, 0.0f, 0.0f};
    device_context->lpVtbl->ClearRenderTargetView(device_context, render_target_view, clear_color);

    device_context->lpVtbl->Draw(device_context, 3, 0);

    save_back_buffer_to_disk();

    if (swapchain->lpVtbl->Present(swapchain, 1, 0) != S_OK) {
        fatal_error("Failed to present swapchain image");
    }
}

int main(void) {
    if (glfwInit() != GLFW_TRUE) {
        fatal_error("Failed to initialize GLFW");
    }
    init_window();
    init_device();
    init_shaders();
    init_vertex_buffer();
    init_swapchain();
    draw_frame();
    finalize();
    return 0;
}

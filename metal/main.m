#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GLFW_INCLUDE_NONE /* prevents GLFW from including OpenGL headers */
#define GLFW_EXPOSE_NATIVE_COCOA
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#import <Metal/Metal.h>
#import <QuartzCore/QuartzCore.h>

#define WIDTH 640
#define HEIGHT 480

static GLFWwindow *window = NULL;
static NSWindow *cocoa_window;
static id<MTLDevice> device;
static id<MTLFunction> vert;
static id<MTLFunction> frag;
static id<MTLBuffer> vertex_buffer;
static CAMetalLayer *layer;
static id<MTLRenderPipelineState> pipeline;
static id<MTLCommandQueue> queue;

static void finalize(void) {
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

    window = glfwCreateWindow(WIDTH, HEIGHT, "triangle-metal", NULL, NULL);
    if (window == NULL) {
        fatal_error("Failed to create window");
    }

    cocoa_window = glfwGetCocoaWindow(window);
    if (cocoa_window == nil) {
        fatal_error("Failed to get Cocoa window");
    }
}

static void init_device(void) {
    device = MTLCreateSystemDefaultDevice();
    if (device == nil) {
        fatal_error("Failed to get Metal device");
    }
    printf("Device: %s\n", [[device name] UTF8String]);

}

static id<MTLFunction> compile_shader(const char *entry_point, const char *shader_source_code) {
    MTLCompileOptions *options = [MTLCompileOptions new];
    options.enableLogging = YES;
    NSError *error;
    id<MTLLibrary> library = [device newLibraryWithSource:@(shader_source_code) options:options error:&error];
    if (library == nil) {
        fputs("Failed to compile shader\n", stderr);
        fatal_error([[error localizedDescription] UTF8String]);
    }
    id<MTLFunction> shader = [library newFunctionWithName:@(entry_point)];
    if (shader == nil) {
        fatal_error("Failed to find the requested shader in the Metal library");
    }
    return shader;
}

static void init_shaders(void) {
    vert = compile_shader("vert_main", (
        "#include <metal_stdlib>\n"
        "using namespace metal;\n"
        "struct Attributes {\n"
        "    float2 a_position [[attribute(0)]];\n"
        "    float3 a_color [[attribute(1)]];\n"
        "};\n"
        "struct Varying {\n"
        "    float4 o_position [[position]];\n"
        "    float3 v_color;\n"
        "};\n"
        "vertex Varying vert_main(Attributes attributes [[stage_in]]) {\n"
        "    Varying varying;\n"
        "    varying.o_position = float4(attributes.a_position, 0., 1.);\n"
        "    varying.v_color = attributes.a_color;\n"
        "    return varying;\n"
        "}\n"
    ));
    frag = compile_shader("frag_main", (
        "#include <metal_stdlib>\n"
        "using namespace metal;\n"
        "struct Varying {\n"
        "    float4 o_position [[position]];\n"
        "    float3 v_color;\n"
        "};\n"
        "fragment float4 frag_main(Varying varying [[stage_in]]) {\n"
        "    return float4(varying.v_color.bgr, 1.); // need to swizzle the color channels since we're rendering to an image with BGRA pixel format instead of RGBA\n"
        "}\n"
    ));
}

static void init_vertex_buffer(void) {
    static const float vertex_attributes[] = {
        /* a_position */ /* a_color */
         0.0f,  0.5f,    1.0f, 0.0f, 0.0f,
         0.5f, -0.5f,    0.0f, 1.0f, 0.0f,
        -0.5f, -0.5f,    0.0f, 0.0f, 1.0f,
    };
    vertex_buffer = [device newBufferWithBytes:vertex_attributes length:sizeof vertex_attributes options:MTLResourceStorageModeShared];
    if (vertex_buffer == nil) {
        fatal_error("Failed to create Metal vertex buffer");
    }
}

static void init_layer(void) {
    layer = [CAMetalLayer layer];
    if (layer == nil) {
        fatal_error("Failed to create Metal layer");
    }

    layer.device = device;
    layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
    layer.allowsNextDrawableTimeout = NO;
    cocoa_window.contentView.layer = layer;
    cocoa_window.contentView.wantsLayer = YES;
}

static void init_pipeline(void) {
    MTLVertexDescriptor *vertex_descriptor = [MTLVertexDescriptor vertexDescriptor];
    vertex_descriptor.layouts[0].stride = 5 * sizeof(float);
    /* a_position */
    vertex_descriptor.attributes[0].format = MTLVertexFormatFloat2;
    vertex_descriptor.attributes[0].offset = 0;
    vertex_descriptor.attributes[0].bufferIndex = 0;
    /* a_color */
    vertex_descriptor.attributes[1].format = MTLVertexFormatFloat3;
    vertex_descriptor.attributes[1].offset = 2 * sizeof(float);
    vertex_descriptor.attributes[1].bufferIndex = 0;

    MTLRenderPipelineDescriptor *pipeline_descriptor = [MTLRenderPipelineDescriptor new];
    pipeline_descriptor.vertexFunction = vert;
    pipeline_descriptor.fragmentFunction = frag;
    pipeline_descriptor.vertexDescriptor = vertex_descriptor;
    pipeline_descriptor.colorAttachments[0].pixelFormat = layer.pixelFormat;
    NSError *error;
    pipeline = [device newRenderPipelineStateWithDescriptor:pipeline_descriptor error:&error];
    if (pipeline == nil) {
        fputs("Failed to initialize pipeline\n", stderr);
        fatal_error([[error localizedDescription] UTF8String]);
    }
}

static void init_queue(void) {
    queue = [device newCommandQueue];
    if (queue == nil) {
        fatal_error("Failed to create Metal queue");
    }
}

static void save_texture_to_disk(id<MTLTexture> texture) {
    void *buffer = malloc((size_t)4 * (size_t)WIDTH * (size_t)HEIGHT);
    if (buffer == NULL) {
        fatal_error("Failed to allocate buffer");
    }
    [texture getBytes:buffer bytesPerRow:(size_t)4 * (size_t)WIDTH bytesPerImage:(size_t)4 * (size_t)WIDTH * (size_t)HEIGHT fromRegion:MTLRegionMake2D(0, 0, WIDTH, HEIGHT) mipmapLevel:0 slice:0];
    puts("Writing screenshot to ./triangle.png");
    int result = stbi_write_png("triangle.png", WIDTH, HEIGHT, 4, buffer, 0);
    free(buffer);
    if (result == 0) {
        fatal_error("Failed to write to output file");
    }
}

static void draw_frame(void) {
    @autoreleasepool {
        id<CAMetalDrawable> drawable = [layer nextDrawable];
        if (drawable == nil) {
            fatal_error("Failed to get Metal drawable");
        }

        id<MTLCommandBuffer> command_buffer = [queue commandBuffer];
        if (command_buffer == nil) {
            fatal_error("Failed to get Metal command buffer");
        }

        MTLRenderPassDescriptor *render_pass_descriptor = [MTLRenderPassDescriptor renderPassDescriptor];
        render_pass_descriptor.colorAttachments[0].texture = drawable.texture;
        render_pass_descriptor.colorAttachments[0].loadAction = MTLLoadActionClear;
        render_pass_descriptor.colorAttachments[0].storeAction = MTLStoreActionStore;
        render_pass_descriptor.colorAttachments[0].clearColor = MTLClearColorMake(0.0, 0.0, 0.0, 0.0);
        id<MTLRenderCommandEncoder> command_encoder = [command_buffer renderCommandEncoderWithDescriptor:render_pass_descriptor];
        if (command_encoder == nil) {
            fatal_error("Failed to create Metal command encoder");
        }

        [command_encoder setRenderPipelineState:pipeline];
        [command_encoder setVertexBuffer:vertex_buffer offset:0 atIndex:0];
        [command_encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
        [command_encoder endEncoding];

        [command_buffer presentDrawable:drawable];
        [command_buffer commit];
        [command_buffer waitUntilCompleted];

        save_texture_to_disk(drawable.texture);
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
    init_layer();
    init_pipeline();
    init_queue();
    draw_frame();
    finalize();
    return 0;
}

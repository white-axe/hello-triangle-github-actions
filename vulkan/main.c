#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

#define GLFW_INCLUDE_NONE /* prevents GLFW from including OpenGL headers */
#define GLFW_INCLUDE_VULKAN
#include <volk.h>
#include <GLFW/glfw3.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include "shader.vert.spv.h"
#include "shader.frag.spv.h"

#define WIDTH 640
#define HEIGHT 480

static GLFWwindow *window = NULL;
static VkInstance instance = VK_NULL_HANDLE;
static VkSurfaceKHR surface = VK_NULL_HANDLE;
static VkDevice device = VK_NULL_HANDLE;
static uint32_t queue_family_index;
static VkPhysicalDevice physical_device = VK_NULL_HANDLE;
static VkQueue queue = VK_NULL_HANDLE;
static VkSwapchainKHR swapchain = VK_NULL_HANDLE;
static VkImage *images = NULL;
static VkImageView *image_views = NULL;
static uint32_t image_count;
static VkRenderPass render_pass = VK_NULL_HANDLE;
static VkShaderModule shader_modules[2] = {VK_NULL_HANDLE, VK_NULL_HANDLE};
static VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
static VkPipeline pipeline = VK_NULL_HANDLE;
static VkFramebuffer *framebuffers = NULL;
static VkCommandPool command_pool = VK_NULL_HANDLE;
static VkCommandBuffer command_buffer = VK_NULL_HANDLE;
static VkSemaphore acquire_semaphore = VK_NULL_HANDLE;
static VkSemaphore release_semaphore = VK_NULL_HANDLE;
static VkFence fence = VK_NULL_HANDLE;
static VkBuffer host_buffer = VK_NULL_HANDLE;
static VkDeviceMemory host_buffer_memory = VK_NULL_HANDLE;
static VkBuffer vertex_buffer = VK_NULL_HANDLE;
static VkDeviceMemory vertex_buffer_memory = VK_NULL_HANDLE;
static VkImage host_image = VK_NULL_HANDLE;
static VkDeviceMemory host_image_memory = VK_NULL_HANDLE;

static const char *const required_device_extensions[] = {
    VK_KHR_SWAPCHAIN_EXTENSION_NAME,
};

static const char *const validation_layers[] = {
    "VK_LAYER_KHRONOS_validation",
};

static bool use_validation_layers;

static void finalize(void) {
    if (device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(device);
        if (host_image != VK_NULL_HANDLE) {
            vkDestroyImage(device, host_image, NULL);
        }
        if (host_image_memory != VK_NULL_HANDLE) {
            vkFreeMemory(device, host_image_memory, NULL);
        }
        if (vertex_buffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(device, vertex_buffer, NULL);
        }
        if (vertex_buffer_memory != VK_NULL_HANDLE) {
            vkFreeMemory(device, vertex_buffer_memory, NULL);
        }
        if (host_buffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(device, host_buffer, NULL);
        }
        if (host_buffer_memory != VK_NULL_HANDLE) {
            vkFreeMemory(device, host_buffer_memory, NULL);
        }
        if (fence != VK_NULL_HANDLE) {
            vkDestroyFence(device, fence, NULL);
        }
        if (release_semaphore != VK_NULL_HANDLE) {
            vkDestroySemaphore(device, release_semaphore, NULL);
        }
        if (acquire_semaphore != VK_NULL_HANDLE) {
            vkDestroySemaphore(device, acquire_semaphore, NULL);
        }
        if (command_pool != VK_NULL_HANDLE) {
            vkDestroyCommandPool(device, command_pool, NULL);
        }
        if (framebuffers != NULL) {
            for (size_t i = image_count; i-- != 0;) {
                if (framebuffers[i] != VK_NULL_HANDLE) {
                    vkDestroyFramebuffer(device, framebuffers[i], NULL);
                }
            }
            free(framebuffers);
        }
        if (pipeline != VK_NULL_HANDLE) {
            vkDestroyPipeline(device, pipeline, NULL);
        }
        if (pipeline_layout != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(device, pipeline_layout, NULL);
        }
        for (size_t i = sizeof shader_modules / sizeof *shader_modules; i-- != 0;) {
            if (shader_modules[i] != VK_NULL_HANDLE) {
                vkDestroyShaderModule(device, shader_modules[i], NULL);
            }
        }
        if (render_pass != VK_NULL_HANDLE) {
            vkDestroyRenderPass(device, render_pass, NULL);
        }
        if (image_views != NULL) {
            for (size_t i = image_count; i-- != 0;) {
                if (image_views[i] != VK_NULL_HANDLE) {
                    vkDestroyImageView(device, image_views[i], NULL);
                }
            }
            free(image_views);
        }
        if (swapchain != VK_NULL_HANDLE) {
            vkDestroySwapchainKHR(device, swapchain, NULL);
        }
        vkDestroyDevice(device, NULL);
    }
    if (instance != VK_NULL_HANDLE) {
        if (surface != VK_NULL_HANDLE) {
            vkDestroySurfaceKHR(instance, surface, NULL);
        }
        vkDestroyInstance(instance, NULL);
    }
    volkFinalize();
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

    window = glfwCreateWindow(WIDTH, HEIGHT, "triangle-vulkan", NULL, NULL);
    if (window == NULL) {
        fatal_error("Failed to create window");
    }
}

static void init_instance(void) {
    if (volkInitialize() != VK_SUCCESS) {
        fatal_error("Failed to load Vulkan symbols");
    }
    glfwInitVulkanLoader(vkGetInstanceProcAddr);
    if (glfwVulkanSupported() != GLFW_TRUE) {
        fatal_error("Failed to initialize GLFW Vulkan support");
    }

    /* If Vulkan validation layers are enabled, check if they're supported */
    uint32_t supported_layer_count = 0;
    vkEnumerateInstanceLayerProperties(&supported_layer_count, NULL);
    VkLayerProperties *supported_layers = malloc((size_t)supported_layer_count * sizeof(VkLayerProperties));
    if (supported_layers == NULL) {
        fatal_error("Failed to allocate buffer for Vulkan layers");
    }
    vkEnumerateInstanceLayerProperties(&supported_layer_count, supported_layers);
    uint32_t validation_layer_count = use_validation_layers ? sizeof validation_layers / sizeof *validation_layers : 0;
    for (uint32_t i = 0; i < validation_layer_count; ++i) {
        bool found = false;
        for (uint32_t j = 0; j < supported_layer_count; ++j) {
            if (strcmp(validation_layers[i], supported_layers[j].layerName) == 0) {
                found = true;
                break;
            }
        }
        if (!found) {
            fatal_error("Error: You don't have Vulkan validation layers installed");
        }
    }
    free(supported_layers);

    uint32_t extension_count;
    const char **extensions = glfwGetRequiredInstanceExtensions(&extension_count);

    printf("Required Vulkan extensions: %zu\n", (size_t)extension_count);
    for (uint32_t i = 0; i < extension_count; ++i) {
        printf("    %s\n", extensions[i]);
    }

    static const VkApplicationInfo application_info = {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pNext = NULL,
        .pApplicationName = "triangle-vulkan",
        .applicationVersion = 0,
        .pEngineName = "",
        .engineVersion = 0,
        .apiVersion = VK_API_VERSION_1_0,
    };
    const VkInstanceCreateInfo instance_create_info = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pNext = NULL,
        .flags = 0,
        .pApplicationInfo = &application_info,
        .enabledLayerCount = validation_layer_count,
        .ppEnabledLayerNames = validation_layers,
        .enabledExtensionCount = extension_count,
        .ppEnabledExtensionNames = extensions,
    };
    if (vkCreateInstance(&instance_create_info, NULL, &instance) != VK_SUCCESS) {
        fatal_error("Failed to initialize Vulkan");
    }
    volkLoadInstance(instance);
}

static void init_surface(void) {
    if (glfwCreateWindowSurface(instance, window, NULL, &surface) != VK_SUCCESS) {
        fatal_error("Failed to create window surface");
    }
}

static void init_device(void) {
    uint32_t physical_device_count;
    vkEnumeratePhysicalDevices(instance, &physical_device_count, NULL);
    VkPhysicalDevice *physical_devices = malloc((size_t)physical_device_count * sizeof(VkPhysicalDevice));
    if (physical_devices == NULL) {
        fatal_error("Failed to allocate buffer for Vulkan physical devices");
    }
    vkEnumeratePhysicalDevices(instance, &physical_device_count, physical_devices);
    for (uint32_t i = 0; i < physical_device_count && physical_device == VK_NULL_HANDLE; ++i) {
        /* Check if all extensions listed in `required_device_extensions` are supported by this physical device; don't use this physical device if the extensions are not all supported */
        uint32_t device_extension_count;
        vkEnumerateDeviceExtensionProperties(physical_devices[i], NULL, &device_extension_count, NULL);
        VkExtensionProperties *device_extensions = malloc((size_t)device_extension_count * sizeof(VkExtensionProperties));
        if (device_extensions == NULL) {
            fatal_error("Failed to allocate buffer for Vulkan physical device extensions");
        }
        vkEnumerateDeviceExtensionProperties(physical_devices[i], NULL, &device_extension_count, device_extensions);
        bool all_required_device_extensions_found = true;
        for (size_t j = 0; j < sizeof required_device_extensions / sizeof *required_device_extensions; ++j) {
            bool found = false;
            for (uint32_t k = 0; k < device_extension_count; ++k) {
                if (strcmp(required_device_extensions[j], device_extensions[k].extensionName) == 0) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                all_required_device_extensions_found = false;
                break;
            }
        }
        free(device_extensions);
        if (!all_required_device_extensions_found) {
            continue;
        }

        /* Find the first queue family that supports graphics; don't use this physical device if none of the queue families support graphics */
        uint32_t queue_families_count;
        vkGetPhysicalDeviceQueueFamilyProperties(physical_devices[i], &queue_families_count, NULL);
        VkQueueFamilyProperties *queue_families = malloc((size_t)queue_families_count * sizeof(VkQueueFamilyProperties));
        if (queue_families == NULL) {
            fatal_error("Failed to allocate buffer for Vulkan physical device queue families");
        }
        vkGetPhysicalDeviceQueueFamilyProperties(physical_devices[i], &queue_families_count, queue_families);
        for (uint32_t j = 0; j < queue_families_count; ++j) {
            if (queue_families[j].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
                physical_device = physical_devices[i];
                queue_family_index = j;
                break;
            }
        }
        free(queue_families);
    }
    free(physical_devices);
    if (physical_device == VK_NULL_HANDLE) {
        fatal_error("Failed to detect a suitable Vulkan physical device");
    }

    VkPhysicalDeviceProperties physical_device_properties;
    vkGetPhysicalDeviceProperties(physical_device, &physical_device_properties);
    printf("Vendor ID: 0x%04llx\n", (unsigned long long)physical_device_properties.vendorID);
    printf("Device ID: 0x%04llx\n", (unsigned long long)physical_device_properties.deviceID);
    const char *device_type_string = NULL;
    switch (physical_device_properties.deviceType) {
        case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: device_type_string = "integrated graphics card"; break;
        case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU: device_type_string = "discrete graphics card"; break;
        case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU: device_type_string = "virtual graphics card"; break;
        case VK_PHYSICAL_DEVICE_TYPE_CPU: device_type_string = "software renderer"; break;
        default: device_type_string = "unknown"; break;
    }
    printf("Device type: %lld (%s)\n", (long long)physical_device_properties.deviceType, device_type_string);
    printf("Device name: %s\n", physical_device_properties.deviceName);
    printf("API version: %llu.%llu.%llu\n", (unsigned long long)VK_API_VERSION_MAJOR(physical_device_properties.apiVersion), (unsigned long long)VK_API_VERSION_MINOR(physical_device_properties.apiVersion), (unsigned long long)VK_API_VERSION_PATCH(physical_device_properties.apiVersion));
    printf("Driver version: %llu.%llu.%llu\n", (unsigned long long)VK_API_VERSION_MAJOR(physical_device_properties.driverVersion), (unsigned long long)VK_API_VERSION_MINOR(physical_device_properties.driverVersion), (unsigned long long)VK_API_VERSION_PATCH(physical_device_properties.driverVersion));

    static const float device_queue_create_info_priority = 1.0f;
    const VkDeviceQueueCreateInfo device_queue_create_info = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .pNext = NULL,
        .flags = 0,
        .queueFamilyIndex = queue_family_index,
        .queueCount = 1,
        .pQueuePriorities = &device_queue_create_info_priority,
    };
    static const VkPhysicalDeviceFeatures physical_device_features = {};
    const VkDeviceCreateInfo device_create_info = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pNext = NULL,
        .flags = 0,
        .queueCreateInfoCount = 1,
        .pQueueCreateInfos = &device_queue_create_info,
        .enabledLayerCount = 0,
        .ppEnabledLayerNames = NULL,
        .enabledExtensionCount = sizeof required_device_extensions / sizeof *required_device_extensions,
        .ppEnabledExtensionNames = required_device_extensions,
        .pEnabledFeatures = &physical_device_features,
    };
    if (vkCreateDevice(physical_device, &device_create_info, NULL, &device) != VK_SUCCESS) {
        fatal_error("Failed to create Vulkan device");
    }
    volkLoadDevice(device);

    vkGetDeviceQueue(device, queue_family_index, 0, &queue);
}

static void init_swapchain(void) {
    VkSurfaceCapabilitiesKHR surface_capabilities;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_device, surface, &surface_capabilities);

    const VkSwapchainCreateInfoKHR swapchain_create_info = {
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .pNext = NULL,
        .flags = 0,
        .surface = surface,
        .minImageCount = surface_capabilities.minImageCount,
        .imageFormat = VK_FORMAT_B8G8R8A8_UNORM,
        .imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR,
        .imageExtent = {
            .width = WIDTH,
            .height = HEIGHT,
        },
        .imageArrayLayers = 1,
        .imageUsage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .queueFamilyIndexCount = 0,
        .pQueueFamilyIndices = NULL,
        .preTransform = surface_capabilities.currentTransform,
        .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .presentMode = VK_PRESENT_MODE_FIFO_KHR,
        .clipped = VK_TRUE,
        .oldSwapchain = VK_NULL_HANDLE,
    };
    if (vkCreateSwapchainKHR(device, &swapchain_create_info, NULL, &swapchain) != VK_SUCCESS) {
        fatal_error("Failed to create swapchain");
    }
}

static void init_image_views(void) {
    vkGetSwapchainImagesKHR(device, swapchain, &image_count, NULL);
    images = malloc((size_t)image_count * sizeof(VkImage));
    if (images == NULL) {
        fatal_error("Failed to allocate buffer for Vulkan swapchain images");
    }
    vkGetSwapchainImagesKHR(device, swapchain, &image_count, images);
    image_views = malloc((size_t)image_count * sizeof(VkImageView));
    if (image_views == NULL) {
        fatal_error("Failed to allocate buffer for Vulkan swapchain image views");
    }
    for (uint32_t i = 0; i < image_count; ++i) {
        image_views[i] = VK_NULL_HANDLE;
    }
    for (uint32_t i = 0; i < image_count; ++i) {
        VkImageViewCreateInfo image_create_info = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .pNext = NULL,
            .flags = 0,
            .image = images[i],
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = VK_FORMAT_B8G8R8A8_UNORM,
            .components = {
                .r = VK_COMPONENT_SWIZZLE_IDENTITY,
                .g = VK_COMPONENT_SWIZZLE_IDENTITY,
                .b = VK_COMPONENT_SWIZZLE_IDENTITY,
                .a = VK_COMPONENT_SWIZZLE_IDENTITY,
            },
            .subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1,
            },
        };
        if (vkCreateImageView(device, &image_create_info, NULL, &image_views[i]) != VK_SUCCESS) {
            fatal_error("Failed to create Vulkan image view");
        }
    }
}

static void init_render_pass(void) {
    static const VkAttachmentDescription attachment_description = {
        .flags = 0,
        .format = VK_FORMAT_B8G8R8A8_UNORM,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
    };
    static const VkAttachmentReference attachment_reference = {
        .attachment = 0,
        .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
    };
    static const VkSubpassDescription subpass_description = {
        .flags = 0,
        .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .inputAttachmentCount = 0,
        .pInputAttachments = NULL,
        .colorAttachmentCount = 1,
        .pColorAttachments = &attachment_reference,
        .pResolveAttachments = NULL,
        .pDepthStencilAttachment = NULL,
        .preserveAttachmentCount = 0,
        .pPreserveAttachments = NULL,
    };
    const VkSubpassDependency subpass_dependency = {
        .srcSubpass = VK_SUBPASS_EXTERNAL,
        .dstSubpass = 0,
        .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        .srcAccessMask = 0,
        .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        .dependencyFlags = 0,
    };
    const VkRenderPassCreateInfo render_pass_create_info = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .pNext = NULL,
        .flags = 0,
        .attachmentCount = 1,
        .pAttachments = &attachment_description,
        .subpassCount = 1,
        .pSubpasses = &subpass_description,
        .dependencyCount = 1,
        .pDependencies = &subpass_dependency,
    };
    if (vkCreateRenderPass(device, &render_pass_create_info, NULL, &render_pass) != VK_SUCCESS) {
        fatal_error("Failed to create Vulkan render pass");
    }
}

static VkShaderModule create_shader_module(const void *shader_bytecode, size_t shader_bytecode_size_in_bytes) {
    const VkShaderModuleCreateInfo shader_module_create_info = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .pNext = NULL,
        .flags = 0,
        .pCode = shader_bytecode,
        .codeSize = shader_bytecode_size_in_bytes,
    };
    VkShaderModule shader_module;
    if (vkCreateShaderModule(device, &shader_module_create_info, NULL, &shader_module) != VK_SUCCESS) {
        fatal_error("Failed to create Vulkan shader module");
    }
    return shader_module;
}

static void init_pipeline(void) {
    shader_modules[0] = create_shader_module(shader_vert_spv, shader_vert_spv_len);
    shader_modules[1] = create_shader_module(shader_frag_spv, shader_frag_spv_len);
    const VkPipelineShaderStageCreateInfo pipeline_shader_stage_create_infos[sizeof shader_modules / sizeof *shader_modules] = {
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .pNext = NULL,
            .flags = 0,
            .stage = VK_SHADER_STAGE_VERTEX_BIT,
            .module = shader_modules[0],
            .pName = "main",
        },
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .pNext = NULL,
            .flags = 0,
            .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
            .module = shader_modules[1],
            .pName = "main",
        },
    };

    static const VkPipelineLayoutCreateInfo pipeline_layout_create_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .pNext = NULL,
        .flags = 0,
        .setLayoutCount = 0,
        .pSetLayouts = NULL,
        .pushConstantRangeCount = 0,
        .pPushConstantRanges = NULL,
    };
    if (vkCreatePipelineLayout(device, &pipeline_layout_create_info, NULL, &pipeline_layout) != VK_SUCCESS) {
        fatal_error("Failed to create Vulkan pipeline layout");
    }

    static const VkVertexInputBindingDescription vertex_input_binding_description = {
        .binding = 0,
        .stride = 5 * sizeof(float),
        .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
    };
    static const VkVertexInputAttributeDescription vertex_input_attribute_descriptions[] = {
        { /* a_position */
            .location = 0,
            .binding = 0,
            .format = VK_FORMAT_R32G32_SFLOAT,
            .offset = 0,
        },
        { /* a_color */
            .location = 1,
            .binding = 0,
            .format = VK_FORMAT_R32G32B32_SFLOAT,
            .offset = 2 * sizeof(float),
        },
    };
    static const VkPipelineVertexInputStateCreateInfo pipeline_vertex_input_state_create_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .pNext = NULL,
        .flags = 0,
        .vertexBindingDescriptionCount = 1,
        .pVertexBindingDescriptions = &vertex_input_binding_description,
        .vertexAttributeDescriptionCount = sizeof vertex_input_attribute_descriptions / sizeof *vertex_input_attribute_descriptions,
        .pVertexAttributeDescriptions = vertex_input_attribute_descriptions,
    };
    static const VkPipelineInputAssemblyStateCreateInfo pipeline_input_assembly_create_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .pNext = NULL,
        .flags = 0,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        .primitiveRestartEnable = VK_FALSE,
    };
    static const VkPipelineViewportStateCreateInfo pipeline_viewport_state_create_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .pNext = NULL,
        .flags = 0,
        .viewportCount = 1,
        .pViewports = NULL,
        .scissorCount = 1,
        .pScissors = NULL,
    };
    static const VkPipelineRasterizationStateCreateInfo pipeline_rasterization_state_create_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .pNext = NULL,
        .flags = 0,
        .depthClampEnable = VK_FALSE,
        .rasterizerDiscardEnable = VK_FALSE,
        .polygonMode = VK_POLYGON_MODE_FILL,
        .cullMode = VK_CULL_MODE_BACK_BIT,
        .frontFace = VK_FRONT_FACE_CLOCKWISE,
        .depthBiasEnable = VK_FALSE,
        .depthBiasConstantFactor = 0.0f,
        .depthBiasClamp = 0.0f,
        .depthBiasSlopeFactor = 0.0f,
        .lineWidth = 1.0f,
    };
    static const VkPipelineMultisampleStateCreateInfo pipeline_multisample_state_create_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .pNext = NULL,
        .flags = 0,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
        .sampleShadingEnable = VK_FALSE,
        .minSampleShading = 1.0f,
        .pSampleMask = NULL,
        .alphaToCoverageEnable = VK_FALSE,
        .alphaToOneEnable = VK_FALSE,
    };
    static const VkPipelineColorBlendAttachmentState pipeline_color_blend_attachment_state = {
        .blendEnable = VK_TRUE,
        .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
        .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .colorBlendOp = VK_BLEND_OP_ADD,
        .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
        .dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
        .alphaBlendOp = VK_BLEND_OP_ADD,
        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
    };
    static const VkPipelineColorBlendStateCreateInfo pipeline_color_blend_state_create_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .pNext = NULL,
        .flags = 0,
        .logicOpEnable = VK_FALSE,
        .logicOp = VK_LOGIC_OP_COPY,
        .attachmentCount = 1,
        .pAttachments = &pipeline_color_blend_attachment_state,
        .blendConstants = {0.0f, 0.0f, 0.0f, 0.0f},
    };
    static const VkDynamicState dynamic_states[] = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR,
    };
    static const VkPipelineDynamicStateCreateInfo pipeline_dynamic_state_create_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .pNext = NULL,
        .flags = 0,
        .dynamicStateCount = sizeof dynamic_states / sizeof *dynamic_states,
        .pDynamicStates = dynamic_states,
    };
    const VkGraphicsPipelineCreateInfo graphics_pipeline_create_info = {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .pNext = NULL,
        .flags = 0,
        .stageCount = sizeof pipeline_shader_stage_create_infos / sizeof *pipeline_shader_stage_create_infos,
        .pStages = pipeline_shader_stage_create_infos,
        .pVertexInputState = &pipeline_vertex_input_state_create_info,
        .pInputAssemblyState = &pipeline_input_assembly_create_info,
        .pTessellationState = NULL,
        .pViewportState = &pipeline_viewport_state_create_info,
        .pRasterizationState = &pipeline_rasterization_state_create_info,
        .pMultisampleState = &pipeline_multisample_state_create_info,
        .pDepthStencilState = NULL,
        .pColorBlendState = &pipeline_color_blend_state_create_info,
        .pDynamicState = &pipeline_dynamic_state_create_info,
        .layout = pipeline_layout,
        .renderPass = render_pass,
        .subpass = 0,
        .basePipelineHandle = VK_NULL_HANDLE,
        .basePipelineIndex = 0,
    };
    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &graphics_pipeline_create_info, NULL, &pipeline) != VK_SUCCESS) {
        fatal_error("Failed to create Vulkan pipeline");
    }

    for (size_t i = sizeof shader_modules / sizeof *shader_modules; i-- != 0;) {
        vkDestroyShaderModule(device, shader_modules[i], NULL);
        shader_modules[i] = VK_NULL_HANDLE;
    }
}

static void init_framebuffers(void) {
    framebuffers = malloc(image_count * sizeof(VkFramebuffer));
    if (framebuffers == NULL) {
        fatal_error("Failed to allocate buffer for Vulkan framebuffers");
    }
    for (size_t i = 0; i < image_count; ++i) {
        framebuffers[i] = VK_NULL_HANDLE;
    }
    for (size_t i = 0; i < image_count; ++i) {
        VkFramebufferCreateInfo framebuffer_create_info = {
            .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
            .pNext = NULL,
            .flags = 0,
            .renderPass = render_pass,
            .attachmentCount = 1,
            .pAttachments = &image_views[i],
            .width = WIDTH,
            .height = HEIGHT,
            .layers = 1,
        };
        if (vkCreateFramebuffer(device, &framebuffer_create_info, NULL, &framebuffers[i]) != VK_SUCCESS) {
            fatal_error("Failed to create Vulkan framebuffer");
        }
    }
}

static void init_command_pool(void) {
    const VkCommandPoolCreateInfo command_pool_create_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .pNext = NULL,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = queue_family_index,
    };
    if (vkCreateCommandPool(device, &command_pool_create_info, NULL, &command_pool) != VK_SUCCESS) {
        fatal_error("Failed to create Vulkan command pool");
    }

    const VkCommandBufferAllocateInfo command_buffer_allocate_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .pNext = NULL,
        .commandPool = command_pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };
    if (vkAllocateCommandBuffers(device, &command_buffer_allocate_info, &command_buffer) != VK_SUCCESS) {
        fatal_error("Failed to allocate Vulkan command buffer");
    }
}

static void init_sync(void) {
    static const VkSemaphoreCreateInfo semaphore_create_info = {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
        .pNext = NULL,
        .flags = 0,
    };
    if (vkCreateSemaphore(device, &semaphore_create_info, NULL, &acquire_semaphore) != VK_SUCCESS) {
        fatal_error("Failed to create Vulkan acquire semaphore");
    }
    if (vkCreateSemaphore(device, &semaphore_create_info, NULL, &release_semaphore) != VK_SUCCESS) {
        fatal_error("Failed to create Vulkan release semaphore");
    }

    static const VkFenceCreateInfo fence_create_info = {
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        .pNext = NULL,
        .flags = 0,
    };
    if (vkCreateFence(device, &fence_create_info, NULL, &fence) != VK_SUCCESS) {
        fatal_error("Failed to create Vulkan fence");
    }
}

static uint32_t find_memory_type_index(uint32_t memory_type_bits, VkMemoryPropertyFlags memory_property_flags) {
    VkPhysicalDeviceMemoryProperties physical_device_memory_properties;
    vkGetPhysicalDeviceMemoryProperties(physical_device, &physical_device_memory_properties);

    for (uint32_t i = 0; i < physical_device_memory_properties.memoryTypeCount; ++i, memory_type_bits >>= 1) {
        if ((memory_type_bits & 1) == 0) {
            continue;
        }
        if ((physical_device_memory_properties.memoryTypes[i].propertyFlags & memory_property_flags) == memory_property_flags) {
            return i;
        }
    }

    fatal_error("Could not find memory type");
    return -1;
}

static void init_vertex_buffer(void) {
    static const float vertex_attributes[] = {
        /* a_position */ /* a_color */
         0.0f, -0.5f,    1.0f, 0.0f, 0.0f,
         0.5f,  0.5f,    0.0f, 1.0f, 0.0f,
        -0.5f,  0.5f,    0.0f, 0.0f, 1.0f,
    };

    {
        const VkBufferCreateInfo buffer_create_info = {
            .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .pNext = NULL,
            .flags = 0,
            .size = sizeof vertex_attributes,
            .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        };
        if (vkCreateBuffer(device, &buffer_create_info, NULL, &host_buffer) != VK_SUCCESS) {
            fatal_error("Failed to create the host buffer for the Vulkan vertex buffer");
        }

        VkMemoryRequirements memory_requirements;
        vkGetBufferMemoryRequirements(device, host_buffer, &memory_requirements);
        const VkMemoryAllocateInfo memory_allocate_info = {
            .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
            .pNext = NULL,
            .allocationSize = memory_requirements.size,
            .memoryTypeIndex = find_memory_type_index(memory_requirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT),
        };
        if (vkAllocateMemory(device, &memory_allocate_info, NULL, &host_buffer_memory) != VK_SUCCESS) {
            fatal_error("Failed to allocate memory for the host buffer for the Vulkan vertex buffer");
        }

        if (vkBindBufferMemory(device, host_buffer, host_buffer_memory, 0) != VK_SUCCESS) {
            fatal_error("Failed to bind the memory for the host buffer for the Vulkan vertex buffer");
        }
    }

    {
        const VkBufferCreateInfo buffer_create_info = {
            .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .pNext = NULL,
            .flags = 0,
            .size = sizeof vertex_attributes,
            .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        };
        if (vkCreateBuffer(device, &buffer_create_info, NULL, &vertex_buffer) != VK_SUCCESS) {
            fatal_error("Failed to create Vulkan vertex buffer");
        }

        VkMemoryRequirements memory_requirements;
        vkGetBufferMemoryRequirements(device, vertex_buffer, &memory_requirements);
        const VkMemoryAllocateInfo memory_allocate_info = {
            .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
            .pNext = NULL,
            .allocationSize = memory_requirements.size,
            .memoryTypeIndex = find_memory_type_index(memory_requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT),
        };
        if (vkAllocateMemory(device, &memory_allocate_info, NULL, &vertex_buffer_memory) != VK_SUCCESS) {
            fatal_error("Failed to allocate memory for the Vulkan vertex buffer");
        }

        if (vkBindBufferMemory(device, vertex_buffer, vertex_buffer_memory, 0) != VK_SUCCESS) {
            fatal_error("Failed to bind the memory for the Vulkan vertex buffer");
        }
    }

    void *host_buffer_memory_mapped;
    if (vkMapMemory(device, host_buffer_memory, 0, sizeof vertex_attributes, 0, &host_buffer_memory_mapped) != VK_SUCCESS) {
        fatal_error("Failed to map the memory for the host buffer for the Vulkan vertex buffer");
    }
    memcpy(host_buffer_memory_mapped, vertex_attributes, sizeof vertex_attributes);
    vkUnmapMemory(device, host_buffer_memory);

    const VkCommandBufferBeginInfo command_buffer_begin_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .pNext = NULL,
        .flags = 0,
        .pInheritanceInfo = NULL,
    };
    if (vkBeginCommandBuffer(command_buffer, &command_buffer_begin_info) != VK_SUCCESS) {
        fatal_error("Failed to begin Vulkan command buffer");
    }

    static const VkBufferCopy buffer_copy = {
        .srcOffset = 0,
        .dstOffset = 0,
        .size = sizeof vertex_attributes,
    };
    vkCmdCopyBuffer(command_buffer, host_buffer, vertex_buffer, 1, &buffer_copy);

    if (vkEndCommandBuffer(command_buffer) != VK_SUCCESS) {
        fatal_error("Failed to end Vulkan command buffer");
    }

    const VkSubmitInfo submit_info = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .pNext = NULL,
        .waitSemaphoreCount = 0,
        .pWaitSemaphores = NULL,
        .pWaitDstStageMask = NULL,
        .commandBufferCount = 1,
        .pCommandBuffers = &command_buffer,
        .signalSemaphoreCount = 0,
        .pSignalSemaphores = NULL,
    };
    if (vkQueueSubmit(queue, 1, &submit_info, fence) != VK_SUCCESS) {
        fatal_error("Failed to submit to Vulkan queue");
    }

    if (vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX) != VK_SUCCESS) {
        fatal_error("Failed to wait for Vulkan fence");
    }

    vkDestroyBuffer(device, host_buffer, NULL);
    host_buffer = NULL;
    vkFreeMemory(device, host_buffer_memory, NULL);
    host_buffer_memory = NULL;
}

static void save_image_to_disk(VkImage image) {
    const VkImageCreateInfo image_create_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .pNext = NULL,
        .flags = 0,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = VK_FORMAT_R8G8B8A8_UNORM,
        .extent = {
            .width = WIDTH,
            .height = HEIGHT,
            .depth = 1,
        },
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_LINEAR,
        .usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .queueFamilyIndexCount = 0,
        .pQueueFamilyIndices = NULL,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    if (vkCreateImage(device, &image_create_info, NULL, &host_image) != VK_SUCCESS) {
        fatal_error("Failed to create host image");
    }

    VkMemoryRequirements memory_requirements;
    vkGetImageMemoryRequirements(device, host_image, &memory_requirements);
    const VkMemoryAllocateInfo memory_allocate_info = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .pNext = NULL,
        .allocationSize = memory_requirements.size,
        .memoryTypeIndex = find_memory_type_index(memory_requirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT),
    };
    if (vkAllocateMemory(device, &memory_allocate_info, NULL, &host_image_memory) != VK_SUCCESS) {
        fatal_error("Failed to allocate memory for the host image");
    }

    if (vkBindImageMemory(device, host_image, host_image_memory, 0) != VK_SUCCESS) {
        fatal_error("Failed to bind the memory for the host image");
    }

    if (vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX) != VK_SUCCESS) {
        fatal_error("Failed to wait for Vulkan fence");
    }
    if (vkResetFences(device, 1, &fence) != VK_SUCCESS) {
        fatal_error("Failed to reset Vulkan fence");
    }

    const VkCommandBufferBeginInfo command_buffer_begin_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .pNext = NULL,
        .flags = 0,
        .pInheritanceInfo = NULL,
    };
    if (vkBeginCommandBuffer(command_buffer, &command_buffer_begin_info) != VK_SUCCESS) {
        fatal_error("Failed to begin Vulkan command buffer");
    }

    {
        const VkImageMemoryBarrier image_memory_barriers[] = {
            {
                .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                .pNext = NULL,
                .srcAccessMask = VK_ACCESS_MEMORY_READ_BIT,
                .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
                .oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                .newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                .srcQueueFamilyIndex = 0,
                .dstQueueFamilyIndex = 0,
                .image = image,
                .subresourceRange = {
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .baseMipLevel = 0,
                    .levelCount = 1,
                    .baseArrayLayer = 0,
                    .layerCount = 1,
                },
            },
            {
                .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                .pNext = NULL,
                .srcAccessMask = 0,
                .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
                .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                .srcQueueFamilyIndex = 0,
                .dstQueueFamilyIndex = 0,
                .image = host_image,
                .subresourceRange = {
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .baseMipLevel = 0,
                    .levelCount = 1,
                    .baseArrayLayer = 0,
                    .layerCount = 1,
                },
            },
        };
        vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, sizeof image_memory_barriers / sizeof *image_memory_barriers, image_memory_barriers);
    }

    static const VkImageCopy image_copy = {
        .srcSubresource = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .mipLevel = 0,
            .baseArrayLayer = 0,
            .layerCount = 1,
        },
        .srcOffset = {
            .x = 0,
            .y = 0,
            .z = 0,
        },
        .dstSubresource = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .mipLevel = 0,
            .baseArrayLayer = 0,
            .layerCount = 1,
        },
        .dstOffset = {
            .x = 0,
            .y = 0,
            .z = 0,
        },
        .extent = {
            .width = WIDTH,
            .height = HEIGHT,
            .depth = 1,
        },
    };
    vkCmdCopyImage(command_buffer, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, host_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &image_copy);

    {
        const VkImageMemoryBarrier image_memory_barriers[] = {
            {
                .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                .pNext = NULL,
                .srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
                .dstAccessMask = VK_ACCESS_MEMORY_READ_BIT,
                .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                .newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                .srcQueueFamilyIndex = 0,
                .dstQueueFamilyIndex = 0,
                .image = image,
                .subresourceRange = {
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .baseMipLevel = 0,
                    .levelCount = 1,
                    .baseArrayLayer = 0,
                    .layerCount = 1,
                },
            },
            {
                .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                .pNext = NULL,
                .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
                .dstAccessMask = VK_ACCESS_MEMORY_READ_BIT,
                .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                .newLayout = VK_IMAGE_LAYOUT_GENERAL,
                .srcQueueFamilyIndex = 0,
                .dstQueueFamilyIndex = 0,
                .image = host_image,
                .subresourceRange = {
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .baseMipLevel = 0,
                    .levelCount = 1,
                    .baseArrayLayer = 0,
                    .layerCount = 1,
                },
            },
        };
        vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, sizeof image_memory_barriers / sizeof *image_memory_barriers, image_memory_barriers);
    }

    if (vkEndCommandBuffer(command_buffer) != VK_SUCCESS) {
        fatal_error("Failed to end Vulkan command buffer");
    }

    const VkSubmitInfo submit_info = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .pNext = NULL,
        .waitSemaphoreCount = 0,
        .pWaitSemaphores = NULL,
        .pWaitDstStageMask = NULL,
        .commandBufferCount = 1,
        .pCommandBuffers = &command_buffer,
        .signalSemaphoreCount = 0,
        .pSignalSemaphores = NULL,
    };
    if (vkQueueSubmit(queue, 1, &submit_info, fence) != VK_SUCCESS) {
        fatal_error("Failed to submit to Vulkan queue");
    }

    if (vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX) != VK_SUCCESS) {
        fatal_error("Failed to wait for Vulkan fence");
    }

    vkDestroyImage(device, host_image, NULL);
    host_image = NULL;

    void *host_image_memory_mapped;
    if (vkMapMemory(device, host_image_memory, 0, (size_t)4 * (size_t)WIDTH * (size_t)HEIGHT, 0, &host_image_memory_mapped) != VK_SUCCESS) {
        fatal_error("Failed to map the memory for the host image");
    }
    puts("Writing screenshot to ./triangle.png");
    int result = stbi_write_png("triangle.png", WIDTH, HEIGHT, 4, host_image_memory_mapped, 0);
    vkUnmapMemory(device, host_image_memory);
    if (result == 0) {
        fatal_error("Failed to write to output file");
    }

    vkFreeMemory(device, host_image_memory, NULL);
    host_image_memory = NULL;
}

static void draw_frame(void) {
    uint32_t image_index;
    if (vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, acquire_semaphore, VK_NULL_HANDLE, &image_index) != VK_SUCCESS) {
        fatal_error("Failed to acquire Vulkan swapchain image");
    }

    if (vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX) != VK_SUCCESS) {
        fatal_error("Failed to wait for Vulkan fence");
    }
    if (vkResetFences(device, 1, &fence) != VK_SUCCESS) {
        fatal_error("Failed to reset Vulkan fence");
    }

    const VkCommandBufferBeginInfo command_buffer_begin_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .pNext = NULL,
        .flags = 0,
        .pInheritanceInfo = NULL,
    };
    if (vkBeginCommandBuffer(command_buffer, &command_buffer_begin_info) != VK_SUCCESS) {
        fatal_error("Failed to begin Vulkan command buffer");
    }

    static const VkClearValue clear_value = {.color = {{0.0f, 0.0f, 0.0f, 0.0f}}};
    const VkRenderPassBeginInfo render_pass_begin_info = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .pNext = NULL,
        .renderPass = render_pass,
        .framebuffer = framebuffers[image_index],
        .renderArea = {
            .offset = {
                .x = 0,
                .y = 0,
            },
            .extent = {
                .width = WIDTH,
                .height = HEIGHT,
            },
        },
        .clearValueCount = 1,
        .pClearValues = &clear_value,
    };
    vkCmdBeginRenderPass(command_buffer, &render_pass_begin_info, VK_SUBPASS_CONTENTS_INLINE);

    vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

    const VkViewport viewport = {
        .x = 0.0f,
        .y = 0.0f,
        .width = (float)WIDTH,
        .height = (float)HEIGHT,
        .minDepth = 0.0f,
        .maxDepth = 1.0f,
    };
    vkCmdSetViewport(command_buffer, 0, 1, &viewport);

    const VkRect2D scissor = {
        .offset = {
            .x = 0,
            .y = 0,
        },
        .extent = {
            .width = WIDTH,
            .height = HEIGHT,
        },
    };
    vkCmdSetScissor(command_buffer, 0, 1, &scissor);

    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(command_buffer, 0, 1, &vertex_buffer, &offset);

    vkCmdDraw(command_buffer, 3, 1, 0, 0);

    vkCmdEndRenderPass(command_buffer);

    if (vkEndCommandBuffer(command_buffer) != VK_SUCCESS) {
        fatal_error("Failed to end Vulkan command buffer");
    }

    const VkPipelineStageFlags pipeline_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    const VkSubmitInfo submit_info = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .pNext = NULL,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &acquire_semaphore,
        .pWaitDstStageMask = &pipeline_stage,
        .commandBufferCount = 1,
        .pCommandBuffers = &command_buffer,
        .signalSemaphoreCount = 1,
        .pSignalSemaphores = &release_semaphore,
    };
    if (vkQueueSubmit(queue, 1, &submit_info, fence) != VK_SUCCESS) {
        fatal_error("Failed to submit to Vulkan queue");
    }

    save_image_to_disk(images[image_index]);

    const VkPresentInfoKHR present_info = {
        .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .pNext = NULL,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &release_semaphore,
        .swapchainCount = 1,
        .pSwapchains = &swapchain,
        .pImageIndices = &image_index,
        .pResults = NULL,
    };
    if (vkQueuePresentKHR(queue, &present_info) != VK_SUCCESS) {
        fatal_error("Failed to present Vulkan queue");
    }
}

int main(int argc, char **argv) {
    if (argc != 2 || (strcmp(argv[1], "debug") != 0 && strcmp(argv[1], "release") != 0)) {
        fprintf(stderr, "Usage:\n");
        fprintf(stderr, "    %s release  # to run without Vulkan validation layers\n", argv[0]);
        fprintf(stderr, "    %s debug  # to run with Vulkan validation layers\n", argv[0]);
        return 1;
    }
    use_validation_layers = strcmp(argv[1], "debug") == 0;

    if (glfwInit() != GLFW_TRUE) {
        fatal_error("Failed to initialize GLFW");
    }
    init_window();
    init_instance();
    init_surface();
    init_device();
    init_swapchain();
    init_image_views();
    init_render_pass();
    init_pipeline();
    init_framebuffers();
    init_command_pool();
    init_sync();
    init_vertex_buffer();
    draw_frame();
    finalize();
    return 0;
}

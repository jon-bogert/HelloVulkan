#include "Renderer.h"

#include "Debug.h"

#include <algorithm>
#include <fstream>
#include <vector>

#include "Mathmatics.h"

void Renderer::Init()
{
    CreateWindow();
    CreateDevice();
    VkFormat swapchainFormat{};
    CreateSwapchain(swapchainFormat);
    CreateRenderPass(swapchainFormat);
    CreateBuffers();
    CreatePipeline();
    CreateFramebuffers();
}

void Renderer::Shutdown()
{
    vkDeviceWaitIdle(m_device);

    for (VkFramebuffer& framebuffer : m_framebuffers)
    {
        vkDestroyFramebuffer(m_device, framebuffer, nullptr);
    }
    m_framebuffers.clear();

    for (PerFrameData& perFrame : m_perFrameData)
    {
        DestroyPerFrameData(perFrame);
    }
    m_perFrameData.clear();

    for (VkSemaphore& semaphore : m_recycledSemaphores)
    {
        vkDestroySemaphore(m_device, semaphore, nullptr);
    }
    m_recycledSemaphores.clear();

    if (m_graphicsPipeline != nullptr)
    {
        vkDestroyPipeline(m_device, m_graphicsPipeline, nullptr);
        m_graphicsPipeline = nullptr;
    }

    if (m_pipelineLayout != nullptr)
    {
        vkDestroyPipelineLayout(m_device, m_pipelineLayout, nullptr);
        m_pipelineLayout = nullptr;
    }

    if (m_indexBuffer.handle != nullptr)
    {
        vkDestroyBuffer(m_device, m_indexBuffer.handle, nullptr);
        m_indexBuffer.handle = nullptr;
    }

    if (m_vertexBuffer.handle != nullptr)
    {
        vkDestroyBuffer(m_device, m_vertexBuffer.handle, nullptr);
        m_vertexBuffer.handle = nullptr;
    }

    if (m_renderPass != nullptr)
    {
        vkDestroyRenderPass(m_device, m_renderPass, nullptr);
        m_renderPass = nullptr;
    }

    for (VkImageView& imageView : m_imageViews)
    {
        (vkDestroyImageView(m_device, imageView, nullptr));
    }
    m_imageViews.clear();

    if (m_swapchain != nullptr)
    {
        vkDestroySwapchainKHR(m_device, m_swapchain, nullptr);
        m_swapchain = nullptr;
    }

    if (m_surface != nullptr)
    {
        vkDestroySurfaceKHR(m_vulkan, m_surface, nullptr);
        m_surface = nullptr;
    }

    if (m_device != nullptr)
    {
        vkDestroyDevice(m_device, nullptr);
    }

    m_graphicsFamilyIndex = -1;

    vkDestroyInstance(m_vulkan, nullptr);
    glfwDestroyWindow(m_window);
    glfwTerminate();
}

void Renderer::Run()
{
    while (!glfwWindowShouldClose(m_window))
    {
        glfwPollEvents();
        Update(1/60.f);
    }
}

VkShaderModule Renderer::LoadShader(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    ASSERT(file.is_open(), "Shader file at " + path.generic_string() + " was not found");

    size_t size = file.tellg();
    file.seekg(std::ios::beg);

    std::vector<uint8_t> buffer(size);
    file.read((char*)buffer.data(), size);

    file.close();

    VkShaderModuleCreateInfo moduleInfo{};
    moduleInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    moduleInfo.codeSize = size;
    moduleInfo.pCode = (uint32_t*)buffer.data();

    VkShaderModule module = nullptr;
    VkResult result = vkCreateShaderModule(m_device, &moduleInfo, nullptr, &module);
    ASSERT(result == VK_SUCCESS, "Unable to create shader module from " + path.generic_string());

    return module;
}

void Renderer::InitPerFrameData(PerFrameData& perFrame)
{
    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    VkResult result = vkCreateFence(m_device, &fenceInfo, nullptr, &perFrame.queueSubmitFence);
    ASSERT(result == VK_SUCCESS, "Could not create Queue Submit Fence");

    VkCommandPoolCreateInfo cmdPoolInfo{};
    cmdPoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    cmdPoolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    cmdPoolInfo.queueFamilyIndex = m_graphicsFamilyIndex;
    result = vkCreateCommandPool(m_device, &cmdPoolInfo, nullptr, &perFrame.primaryCmdPool);
    ASSERT(result == VK_SUCCESS, "Could not create primary command pool");

    VkCommandBufferAllocateInfo cmdBufferInfo{};
    cmdBufferInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cmdBufferInfo.commandPool = perFrame.primaryCmdPool;
    cmdBufferInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmdBufferInfo.commandBufferCount = 1;
    result = vkAllocateCommandBuffers(m_device, &cmdBufferInfo, &perFrame.primaryCmdBuffer);
}

void Renderer::CreateWindow()
{
    // Initialize GLFW Window
    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

    m_window = glfwCreateWindow(m_windowWidth, m_windowHeight, m_windowName.c_str(), nullptr, nullptr);

    // Create Vulkan Instance
    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = m_windowName.c_str();
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = nullptr;
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_0;

    VkInstanceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;

    uint32_t glfwExtensionCount{};
    const char** glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);
    createInfo.enabledExtensionCount = glfwExtensionCount;
    createInfo.ppEnabledExtensionNames = glfwExtensions;
    createInfo.enabledLayerCount = 0;

    VkResult result = vkCreateInstance(&createInfo, nullptr, &m_vulkan);
    ASSERT(result == VK_SUCCESS, "Unable to create Vulkan instance");
    result = glfwCreateWindowSurface(m_vulkan, m_window, nullptr, &m_surface);
}

void Renderer::CreateDevice()
{
    // Get GPU's (Physical Devices)
    uint32_t deviceCount{};
    vkEnumeratePhysicalDevices(m_vulkan, &deviceCount, nullptr); // Just a test
    ASSERT(deviceCount != 0, "Could not find GPU's with Vulkan support");

    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(m_vulkan, &deviceCount, devices.data());

    for (VkPhysicalDevice device : devices)
    {
        VkPhysicalDeviceProperties props{};
        vkGetPhysicalDeviceProperties(device, &props);
        LOG(std::string("Device found: ") + props.deviceName);
    }
    LOG("Selecting first device");
    m_gpu = devices[0];

    uint32_t deviceExtensionCount{};
    vkEnumerateDeviceExtensionProperties(m_gpu, nullptr, &deviceExtensionCount, nullptr);
    std::vector<VkExtensionProperties> deviceExtensions(deviceExtensionCount);
    vkEnumerateDeviceExtensionProperties(m_gpu, nullptr, &deviceExtensionCount, deviceExtensions.data());
    std::vector<const char*> requiredExtensions{ VK_KHR_SWAPCHAIN_EXTENSION_NAME };
    for (const char* req : requiredExtensions)
    {
        ASSERT(std::find_if(deviceExtensions.begin(),
            deviceExtensions.end(),
            [=](const VkExtensionProperties& ext) { return strcmp(req, ext.extensionName) == 0; }
        ) != deviceExtensions.end(), "Required extensions not found: " + std::string(req));
    }
    

    // Create Logical Device (interface)

    VkDeviceQueueCreateInfo queueCreateInfo{};
    queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueCreateInfo.queueFamilyIndex = 0;
    queueCreateInfo.queueCount = 1;
    float queuePriority = 1.f;
    queueCreateInfo.pQueuePriorities = &queuePriority;

    VkPhysicalDeviceFeatures deviceFeatures{};

    VkDeviceCreateInfo deviceInfo{};
    deviceInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    deviceInfo.pQueueCreateInfos = &queueCreateInfo;
    deviceInfo.queueCreateInfoCount = 1;
    deviceInfo.pEnabledFeatures = &deviceFeatures;
    deviceInfo.enabledExtensionCount = (uint32_t)requiredExtensions.size();
    deviceInfo.ppEnabledExtensionNames = requiredExtensions.data();

    VkResult result = vkCreateDevice(m_gpu, &deviceInfo, nullptr, &m_device);
    ASSERT(result == VK_SUCCESS, "Could not create Vulkan logical device");

    vkGetDeviceQueue(m_device, 0, 0, &m_deviceQueue);
}

void Renderer::CreateSwapchain(VkFormat& out_swapchainFormat)
{
    // Query Capabilities
    VkSurfaceCapabilitiesKHR capabilities{};
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_gpu, m_surface, &capabilities);

    uint32_t formatCount{};
    vkGetPhysicalDeviceSurfaceFormatsKHR(m_gpu, m_surface, &formatCount, nullptr); // call with null to get count
    std::vector<VkSurfaceFormatKHR> formats(formatCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(m_gpu, m_surface, &formatCount, formats.data()); // populate

    uint32_t presentModeCount{};
    vkGetPhysicalDeviceSurfacePresentModesKHR(m_gpu, m_surface, &presentModeCount, nullptr);
    std::vector<VkPresentModeKHR> presentModes(presentModeCount);
    vkGetPhysicalDeviceSurfacePresentModesKHR(m_gpu, m_surface, &presentModeCount, presentModes.data());

    // Select Format and Presentation Mode
    VkSurfaceFormatKHR surfaceFormat = formats[0]; // default to first if prefered (SRGB) not found;
    for (const VkSurfaceFormatKHR& format : formats)
    {
        if (format.format == VK_FORMAT_R8G8B8A8_SRGB && format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
        {
            surfaceFormat = format;
            break;
        }
    }
    out_swapchainFormat = surfaceFormat.format; // Carry forward to Render Pass Creation

    VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR; // prefer mailbox/triple buffering (hybrid), fallback to FIFO (aka vsync on) (immediate == vsync off)
    for (const VkPresentModeKHR& mode : presentModes)
    {
        if (mode == VK_PRESENT_MODE_MAILBOX_KHR)
        {
            presentMode = mode;
            break;
        }
    }

    // Set swapchain resolution
    VkExtent2D extent = capabilities.currentExtent;
    if (capabilities.currentExtent.width == UINT32_MAX)
    {
        extent.width = std::clamp(m_windowWidth, capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
        extent.height = std::clamp(m_windowHeight, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);
    }

    // Choose image count (prefer triple buffering)
    uint32_t imageCount = capabilities.minImageCount + 1;
    if (capabilities.maxImageCount > 0 && imageCount > capabilities.maxImageCount)
    {
        imageCount = capabilities.maxImageCount;
    }

    // Create the swapchain
    VkSwapchainCreateInfoKHR swapchainInfo{};
    swapchainInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    swapchainInfo.surface = m_surface;
    swapchainInfo.minImageCount = imageCount;
    swapchainInfo.imageFormat = surfaceFormat.format;
    swapchainInfo.imageColorSpace = surfaceFormat.colorSpace;
    swapchainInfo.imageExtent = extent;
    swapchainInfo.imageArrayLayers = 1;
    swapchainInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT; // specifies that the image can be used to create a VkImageView suitable for use as a color or resolve attachment in a VkFramebuffer.
    swapchainInfo.preTransform = capabilities.currentTransform;
    swapchainInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR; // indicates the alpha compositing mode to use when this surface is composited together with other surfaces on certain window systems.
    swapchainInfo.clipped = VK_TRUE;
    swapchainInfo.oldSwapchain = nullptr;


    uint32_t queueFamilyCount{};
    vkGetPhysicalDeviceQueueFamilyProperties(m_gpu, &queueFamilyCount, nullptr);
    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(m_gpu, &queueFamilyCount, queueFamilies.data());

    int i = 0;
    int graphicsFamily = -1;
    int presentFamily = -1;
    for (const VkQueueFamilyProperties& queueFamily : queueFamilies)
    {
        if (queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT)
            graphicsFamily = i;

        VkBool32 presentSupport = false;
        vkGetPhysicalDeviceSurfaceSupportKHR(m_gpu, i, m_surface, &presentSupport);
        if (presentSupport)
            presentFamily = i;
    }

    ASSERT(graphicsFamily >= 0, "Graphics family not found");
    ASSERT(presentFamily >= 0, "Present family not found");
    m_graphicsFamilyIndex = graphicsFamily;

    // Handle queue family sharing if graphics and present queues differ
    uint32_t queueFamilyIndices[2] = { graphicsFamily, presentFamily };
    if (graphicsFamily != presentFamily)
    {
        swapchainInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        swapchainInfo.queueFamilyIndexCount = 2;
        swapchainInfo.pQueueFamilyIndices = queueFamilyIndices;
    }
    else
    {
        swapchainInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        swapchainInfo.queueFamilyIndexCount = 0;
        swapchainInfo.pQueueFamilyIndices = nullptr;
    }

    VkResult result = vkCreateSwapchainKHR(m_device, &swapchainInfo, nullptr, &m_swapchain);
    ASSERT(result == VK_SUCCESS, "Vulkan swapchain could not be created");

    vkGetSwapchainImagesKHR(m_device, m_swapchain, &imageCount, nullptr);
    std::vector<VkImage> swapchainImages(imageCount);
    vkGetSwapchainImagesKHR(m_device, m_swapchain, &imageCount, swapchainImages.data());

    m_perFrameData.clear();
    m_perFrameData.resize(imageCount);
    for (size_t i = 0; i < imageCount; ++i)
    {
        InitPerFrameData(m_perFrameData[i]);
    }

    m_imageViews.clear();
    for (size_t i = 0; i < imageCount; ++i)
    {
        VkImageViewCreateInfo imgViewInfo{};
        imgViewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        imgViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        imgViewInfo.format = out_swapchainFormat;
        imgViewInfo.image = swapchainImages[i];
        imgViewInfo.subresourceRange.levelCount = 1;
        imgViewInfo.subresourceRange.layerCount = 1;
        imgViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        imgViewInfo.components.r = VK_COMPONENT_SWIZZLE_R;
        imgViewInfo.components.g = VK_COMPONENT_SWIZZLE_G;
        imgViewInfo.components.b = VK_COMPONENT_SWIZZLE_B;
        imgViewInfo.components.a = VK_COMPONENT_SWIZZLE_A;

        VkImageView imageView{};
        result = vkCreateImageView(m_device, &imgViewInfo, nullptr, &imageView);
        ASSERT(result == VK_SUCCESS, "Could not create Image view " + std::to_string(i));
        m_imageViews.push_back(imageView);
    }

}

void Renderer::CreateRenderPass(const VkFormat swapchainFormat)
{
    VkAttachmentDescription attachment{};
    attachment.format = swapchainFormat;
    attachment.samples = VK_SAMPLE_COUNT_1_BIT; // No multisampling
    attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE; // Not using
    attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE; // Not using
    attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference colorReference{};
    colorReference.attachment = 0;
    colorReference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.flags = 0;
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorReference;

    VkSubpassDependency subpassDependency{};
    subpassDependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    subpassDependency.dstSubpass = 0;
    subpassDependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    subpassDependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    subpassDependency.srcAccessMask = 0;
    subpassDependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = 1;
    renderPassInfo.pAttachments = &attachment;
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = 1;
    renderPassInfo.pDependencies = &subpassDependency;

    VkResult result = vkCreateRenderPass(m_device, &renderPassInfo, nullptr, &m_renderPass);
}

void Renderer::CreateBuffers()
{
    m_vertexBuffer.usageFlags = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    m_indexBuffer.usageFlags = VK_BUFFER_USAGE_INDEX_BUFFER_BIT;

    Vector3 vertexData[3] =
    {
        {-0.5f, -0.5f, 0.0f},
        {0.0f, 0.5f, 0.0f},
        {0.5f, -0.5f, 0.0f}
    };

    uint32_t indexData[3] = { 0, 1, 2 };

    CreateOrResizeBuffer(m_vertexBuffer, sizeof(vertexData));
    CreateOrResizeBuffer(m_indexBuffer, sizeof(indexData));

    Vector3* vertexBufferMemory;
    VkResult result = vkMapMemory(m_device, m_vertexBuffer.memory, 0, sizeof(vertexData), 0, (void**)(&vertexBufferMemory));
    ASSERT(result == VK_SUCCESS, "Could not map vertex buffer memory");
    memcpy(vertexBufferMemory, vertexData, sizeof(vertexData));

    uint32_t* indexBufferMemory;
    result = vkMapMemory(m_device, m_indexBuffer.memory, 0, sizeof(indexData), 0, (void**)(&indexBufferMemory));
    ASSERT(result == VK_SUCCESS, "Could not map index buffer memory");
    memcpy(indexBufferMemory, indexData, sizeof(indexData));

    VkMappedMemoryRange ranges[2]{};
    ranges[0].sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
    ranges[0].memory = m_vertexBuffer.memory;
    ranges[0].size = VK_WHOLE_SIZE;
    ranges[1].sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
    ranges[1].memory = m_indexBuffer.memory;
    ranges[1].size = VK_WHOLE_SIZE;

    result = vkFlushMappedMemoryRanges(m_device, 2, ranges); // Flushing writes data to GPU
    ASSERT(result == VK_SUCCESS, "Could not flush vertex and index buffer memory");

    vkUnmapMemory(m_device, m_vertexBuffer.memory);
    vkUnmapMemory(m_device, m_indexBuffer.memory);
}


void Renderer::CreatePipeline()
{
    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    VkResult result = vkCreatePipelineLayout(m_device, &layoutInfo, nullptr, &m_pipelineLayout);
    ASSERT(result == VK_SUCCESS, "Could not create pipeline layout");
    
    // Set up Vertex/Index buffer binding
    VkVertexInputBindingDescription bindingDesc[1]{};
    bindingDesc[0].stride = sizeof(Vector3);
    bindingDesc[0].binding = 0;
    bindingDesc[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription attributeDesc[1]{}; // Add More for more attributes in a vertex (pos, uv, col, etc)
    attributeDesc[0].location = 0;
    attributeDesc[0].binding = bindingDesc[0].binding;
    attributeDesc[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributeDesc[0].offset = 0;

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.pVertexBindingDescriptions = bindingDesc;
    vertexInputInfo.vertexAttributeDescriptionCount = 1;
    vertexInputInfo.pVertexAttributeDescriptions = attributeDesc;

    // Specify triangles
    VkPipelineInputAssemblyStateCreateInfo inputAssemblyInfo{};
    inputAssemblyInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssemblyInfo.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    // Rasterizer Options
    VkPipelineRasterizationStateCreateInfo rasterizerInfo{};
    rasterizerInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizerInfo.cullMode = VK_CULL_MODE_BACK_BIT;
    rasterizerInfo.frontFace = VK_FRONT_FACE_CLOCKWISE;
    rasterizerInfo.lineWidth = 1.f;

    // Color writing (no blending)
    VkPipelineColorBlendAttachmentState blendAttachment{};
    blendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo blendInfo{};
    blendInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    blendInfo.attachmentCount = 1;
    blendInfo.pAttachments = &blendAttachment;

    // Specify Viewport and scissor (scissor is the clipping area)
    VkPipelineViewportStateCreateInfo viewportInfo{};
    viewportInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportInfo.viewportCount = 1;
    viewportInfo.scissorCount = 1; // must be same as viewport count

    // Depth Testing Stencil (disabled)
    VkPipelineDepthStencilStateCreateInfo depthStencilInfo{};
    depthStencilInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;

    // Multisampling (diabled)
    VkPipelineMultisampleStateCreateInfo multisampleInfo{};
    multisampleInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampleInfo.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    // Specify that that the viewport and scissor will dynamic (not a part of the pipeline)
    VkDynamicState dynamics[2] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };

    VkPipelineDynamicStateCreateInfo dynamicInfo{};
    dynamicInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicInfo.pDynamicStates = dynamics;
    dynamicInfo.dynamicStateCount = 2;

    // Load Shaders
    VkPipelineShaderStageCreateInfo shaderStages[2] = { VkPipelineShaderStageCreateInfo(), VkPipelineShaderStageCreateInfo() };

    //Vertex
    VkPipelineShaderStageCreateInfo& vertexShader = shaderStages[0];
    vertexShader.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertexShader.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertexShader.module = LoadShader("Assets/Shaders/bin/basic.vert.spirv");
    vertexShader.pName = "main";

    //Fragment (Pixel)
    VkPipelineShaderStageCreateInfo& fragmentShader = shaderStages[1];
    fragmentShader.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragmentShader.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragmentShader.module = LoadShader("Assets/Shaders/bin/basic.frag.spirv");
    fragmentShader.pName = "main";

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2; // vert & frag
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssemblyInfo;
    pipelineInfo.pRasterizationState = &rasterizerInfo;
    pipelineInfo.pColorBlendState = &blendInfo;
    pipelineInfo.pMultisampleState = &multisampleInfo;
    pipelineInfo.pViewportState = &viewportInfo;
    pipelineInfo.pDepthStencilState = &depthStencilInfo;
    pipelineInfo.pDynamicState = &dynamicInfo;

    pipelineInfo.renderPass = m_renderPass;
    pipelineInfo.layout = m_pipelineLayout;

    result = vkCreateGraphicsPipelines(m_device, nullptr, 1, &pipelineInfo, nullptr, &m_graphicsPipeline);
    ASSERT(result == VK_SUCCESS, "Could not create Vulkan graphics pipeline");

    //Pipeline is created, we can now delete the shader modules
    vkDestroyShaderModule(m_device, vertexShader.module, nullptr);
    vkDestroyShaderModule(m_device, fragmentShader.module, nullptr);
}

void Renderer::CreateFramebuffers()
{
    m_framebuffers.clear();

    for (VkImageView& imageView : m_imageViews)
    {
        VkFramebufferCreateInfo framebufferInfo{};
        framebufferInfo.renderPass = m_renderPass;
        framebufferInfo.attachmentCount = 1;
        framebufferInfo.pAttachments = &imageView;
        framebufferInfo.width = m_windowWidth;
        framebufferInfo.height = m_windowHeight;
        framebufferInfo.layers = 1;

        VkFramebuffer framebuffer;
        VkResult result = vkCreateFramebuffer(m_device, &framebufferInfo, nullptr, &framebuffer);
        ASSERT(result == VK_SUCCESS, "Could not create framebuffer");

        m_framebuffers.push_back(framebuffer);
    }
}

void Renderer::CreateOrResizeBuffer(Buffer& buffer, uint64_t newSize)
{
    if (buffer.handle != nullptr)
    {
        vkDestroyBuffer(m_device, buffer.handle, nullptr);
    }
    if (buffer.memory != nullptr)
    {
        vkFreeMemory(m_device, buffer.memory, nullptr);
    }

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = newSize;
    bufferInfo.usage = buffer.usageFlags;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VkResult result = vkCreateBuffer(m_device, &bufferInfo, nullptr, &buffer.handle);
    ASSERT(result == VK_SUCCESS, "Could not create buffer");

    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(m_device, buffer.handle, &req);

    // Allignment if req'd

    VkPhysicalDeviceMemoryProperties gpuProperties;
    vkGetPhysicalDeviceMemoryProperties(m_gpu, &gpuProperties);
    uint32_t memoryType = UINT32_MAX;
    for (uint32_t i = 0; i < gpuProperties.memoryTypeCount; ++i)
    {
        if ((gpuProperties.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)
            == VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT && req.memoryTypeBits & (1 << i))
        {
            memoryType = i;
            break;
        }
    }

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = req.size;
    allocInfo.memoryTypeIndex = memoryType;
    result = vkAllocateMemory(m_device, &allocInfo, nullptr, &buffer.memory);
    ASSERT(result == VK_SUCCESS, "Could not allocate buffer memory");

    result = vkBindBufferMemory(m_device, buffer.handle, buffer.memory, 0);
    ASSERT(result == VK_SUCCESS, "Could not bind buffer memory");

    buffer.size = req.size;
}

VkResult Renderer::NextImage(uint32_t& imageIndex)
{ 
    VkResult result{};
    VkSemaphore aquireSemaphore{};
    if (m_recycledSemaphores.empty())
    {
        VkSemaphoreCreateInfo semaphoreInfo{};
        semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        result = vkCreateSemaphore(m_device, &semaphoreInfo, nullptr, &aquireSemaphore);
        ASSERT(result == VK_SUCCESS, "Could not create new semaphore");
    }
    else
    {
        aquireSemaphore = m_recycledSemaphores.back();
        m_recycledSemaphores.pop_back();
    }

    result = vkAcquireNextImageKHR(m_device, m_swapchain, UINT64_MAX, aquireSemaphore, nullptr, &imageIndex);

    if (result != VK_SUCCESS)
    {
        m_recycledSemaphores.push_back(aquireSemaphore);
        return result;
    }

    if (m_perFrameData[imageIndex].queueSubmitFence != nullptr)
    {
        vkWaitForFences(m_device, 1, &m_perFrameData[imageIndex].queueSubmitFence, true, UINT64_MAX);
        vkResetFences(m_device, 1, &m_perFrameData[imageIndex].queueSubmitFence);
    }

    if (m_perFrameData[imageIndex].primaryCmdPool != nullptr)
    {
        vkResetCommandPool(m_device, m_perFrameData[imageIndex].primaryCmdPool, 0);
    }

    VkSemaphore oldSemaphore = m_perFrameData[imageIndex].swapchainAcquireSemaphore;

    if (oldSemaphore != nullptr)
    {
        m_recycledSemaphores.push_back(oldSemaphore);
    }

    m_perFrameData[imageIndex].swapchainAcquireSemaphore = aquireSemaphore;

    return VK_SUCCESS;
}

void Renderer::Update(const float deltaTime)
{
    uint32_t imageIndex{};
    VkResult result = NextImage(imageIndex);
    if (result == VK_SUBOPTIMAL_KHR || result == VK_ERROR_OUT_OF_DATE_KHR)
    {
        LOG("Swapchain image suboptimal or out of date");
        // resize
        result = NextImage(imageIndex);
    }

    if (result != VK_SUCCESS)
    {
        LOG("Could not get next image, idling...");
        return;
    }

    Render(imageIndex);
    result = Present(imageIndex);

    if (result == VK_SUBOPTIMAL_KHR || result == VK_ERROR_OUT_OF_DATE_KHR)
    {
        LOG("Swapchain image presentation suboptimal or out of date");
        //resize
    }
    else if (result != VK_SUCCESS)
    {
        LOG("Failed to present swapchain image");
    }
}

void Renderer::Render(uint32_t index)
{
    VkFramebuffer framebuffer = m_framebuffers[index];

    VkCommandBuffer cmd = m_perFrameData[index].primaryCmdBuffer;

    VkCommandBufferBeginInfo cmdBeginInfo{};
    cmdBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    cmdBeginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &cmdBeginInfo);

    VkClearValue clearValue{};
    clearValue.color = { 0.01f, 0.01f, 0.01f, 1.f };

    VkRenderPassBeginInfo passBeginInfo{};
    passBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    passBeginInfo.renderPass = m_renderPass;
    passBeginInfo.framebuffer = framebuffer;
    passBeginInfo.renderArea.extent.width = m_windowWidth;
    passBeginInfo.renderArea.extent.height = m_windowHeight;
    passBeginInfo.clearValueCount = 1;
    passBeginInfo.pClearValues = &clearValue;
    vkCmdBeginRenderPass(cmd, &passBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_graphicsPipeline);

    uint64_t offset{ 0 };
    vkCmdBindVertexBuffers(cmd, 0, 1, &m_vertexBuffer.handle, &offset);
    //vkCmdBindIndexBuffers(cmd, 0, 1, &m_indexBuffer.handle, &offset);

    VkViewport viewport{};
    viewport.y = m_windowHeight;
    viewport.width = m_windowWidth;
    viewport.height = -viewport.y;
    viewport.minDepth = 0.f;
    viewport.maxDepth = 1.f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.extent.width = m_windowWidth;
    scissor.extent.height = m_windowHeight;
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    // Draw Commands
    vkCmdDraw(cmd, 3, 1, 0, 0);

    // End Drawing
    vkCmdEndRenderPass(cmd);

    VkResult result = vkEndCommandBuffer(cmd);
    ASSERT(result == VK_SUCCESS, "Could not end command buffer");

    // Send to Queue
    if (m_perFrameData[index].swapchainReleaseSemaphore == nullptr)
    {
        VkSemaphoreCreateInfo semaphoreInfo{};
        semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        result = vkCreateSemaphore(m_device, &semaphoreInfo, nullptr, &m_perFrameData[index].swapchainReleaseSemaphore);
    }

    VkPipelineStageFlags waitStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = &m_perFrameData[index].swapchainAcquireSemaphore;
    submitInfo.pWaitDstStageMask = &waitStageMask;
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = &m_perFrameData[index].swapchainReleaseSemaphore;
    result = vkQueueSubmit(m_deviceQueue, 1, &submitInfo, m_perFrameData[index].queueSubmitFence);
}

VkResult Renderer::Present(uint32_t index)
{
    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &m_swapchain;
    presentInfo.pImageIndices = &index;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &m_perFrameData[index].swapchainReleaseSemaphore;

    return vkQueuePresentKHR(m_deviceQueue, &presentInfo);
}

void Renderer::DestroyPerFrameData(PerFrameData& perFrameData)
{
    if (perFrameData.queueSubmitFence != nullptr)
    {
        vkDestroyFence(m_device, perFrameData.queueSubmitFence, nullptr);
        perFrameData.queueSubmitFence = nullptr;
    }

    if (perFrameData.primaryCmdBuffer != nullptr)
    {
        vkFreeCommandBuffers(m_device, perFrameData.primaryCmdPool, 1, &perFrameData.primaryCmdBuffer);
        perFrameData.primaryCmdBuffer = nullptr;
    }

    if (perFrameData.primaryCmdPool != nullptr)
    {
        vkDestroyCommandPool(m_device, perFrameData.primaryCmdPool, nullptr);
        perFrameData.primaryCmdPool = nullptr;
    }

    if (perFrameData.swapchainAcquireSemaphore != nullptr)
    {
        vkDestroySemaphore(m_device, perFrameData.swapchainAcquireSemaphore, nullptr);
        perFrameData.swapchainAcquireSemaphore = nullptr;
    }

    if (perFrameData.swapchainReleaseSemaphore != nullptr)
    {
        vkDestroySemaphore(m_device, perFrameData.swapchainReleaseSemaphore, nullptr);
        perFrameData.swapchainReleaseSemaphore = nullptr;
    }
}

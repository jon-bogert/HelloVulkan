#pragma once

#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>

#include <filesystem>
#include <string>

class Renderer
{
public:
	void Init();
	void Shutdown();

	void Run();

private:

	struct PerFrameData
	{
		VkFence         queueSubmitFence = nullptr;
		VkCommandPool   primaryCmdPool = nullptr;
		VkCommandBuffer primaryCmdBuffer = nullptr;
		VkSemaphore     swapchainAcquireSemaphore = nullptr;
		VkSemaphore     swapchainReleaseSemaphore = nullptr;
	};

	struct Buffer
	{
		VkBuffer handle = nullptr;
		VkDeviceMemory memory = nullptr;
		VkDeviceSize size = 0;
		VkBufferUsageFlagBits usageFlags = VK_BUFFER_USAGE_FLAG_BITS_MAX_ENUM;
	};

	VkShaderModule LoadShader(const std::filesystem::path& path);
	void InitPerFrameData(PerFrameData& perFrame);
	void CreateWindow();
	void CreateDevice();
	void CreateSwapchain(VkFormat& out_swapchainFormat);
	void CreateRenderPass(const VkFormat swapchainFormat);
	void CreateBuffers();
	void CreatePipeline();
	void CreateFramebuffers();

	void CreateOrResizeBuffer(Buffer& buffer, uint64_t newSize);

	VkResult NextImage(uint32_t& out_imageIndex);
	void Update(const float deltaTime);
	void Render(uint32_t index);
	VkResult Present(uint32_t index);

	void DestroyPerFrameData(PerFrameData& perFrameData);

private:
	VkInstance m_vulkan;
	GLFWwindow* m_window;
	uint32_t m_windowWidth = 800;
	uint32_t m_windowHeight = 600;
	std::string m_windowName = "Hello Vulkan";

	VkPipeline m_graphicsPipeline = nullptr;
	VkPipelineLayout m_pipelineLayout = nullptr;
	VkSwapchainKHR m_swapchain = nullptr;
	VkRenderPass m_renderPass = nullptr;
	VkQueue m_deviceQueue = nullptr;
	VkPhysicalDevice m_gpu = nullptr;
	VkDevice m_device = nullptr;
	VkSurfaceKHR m_surface = nullptr;

	Buffer m_vertexBuffer{};
	Buffer m_indexBuffer{};

	int32_t m_graphicsFamilyIndex = -1;
	std::vector<VkImageView> m_imageViews{};
	std::vector<VkFramebuffer> m_framebuffers{};
	std::vector<PerFrameData> m_perFrameData{};
	std::vector<VkSemaphore> m_recycledSemaphores{};
};


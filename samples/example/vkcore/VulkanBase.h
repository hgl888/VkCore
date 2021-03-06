#pragma once

#ifdef _WIN32
#pragma comment(linker, "/subsystem:windows")
#include <windows.h>
#include <fcntl.h>
#include <io.h>
#elif defined(__ANDROID__)
#include <android/native_activity.h>
#include <android/asset_manager.h>
#include <android_native_app_glue.h>
#include "vulkanandroid.h"
#elif defined(__linux__)
#include <xcb/xcb.h>
#endif

#include <iostream>
#include <chrono>
#include "define.h"
#include <glm/glm.hpp>
#include <string>
#include <array>

#include "vulkan/vulkan.h"

#include "Keyboard.h"
#include "vulkantools.h"
#include "vulkandebug.h"
#include "VkCoreDevice.hpp"
#include "vulkanswapchain.hpp"
#include "vulkanTextureLoader.hpp"
#include "vulkanMeshLoader.hpp"
#include "vulkantextoverlay.hpp"
#include "VkCamera.hpp"
#include "Vector2.h"

// Function pointer for getting physical device fetures to be enabled
typedef VkPhysicalDeviceFeatures (*PFN_GetEnabledFeatures)();

class VulkanBase
{

private:	
	// Set to true when example is created with enabled validation layers
	bool mEnableValidation = false;
	// Set to true if v-sync will be forced for the swapchain
	bool enableVSync = false;	
	// fps timer (one second interval)
	float fpsTimer = 0.0f;

	/** brief Indicates that the view (position, rotation) has changed and */
	bool viewUpdated = false;
	// Destination dimensions for resizing the window
	uint32_t destWidth;
	uint32_t destHeight;
	bool resizing = false;

protected:
	// Last frame time, measured using a high performance timer (if available)
	float frameTimer = 1.0f;
	// Frame counter to display fps
	uint32_t frameCounter = 0;
	uint32_t lastFPS = 0;
	// Vulkan instance, stores all per-application states
	VkInstance mInstance;
	VkCoreDevice *mVulkanDevice;

	// Device features enabled by the example
	// If not set, no additional features are enabled (may result in validation layer errors)
	VkPhysicalDeviceFeatures enabledFeatures = {};

	// Handle to the device graphics queue that command buffers are submitted to
	VkQueue mQueue;
	// Color buffer format
	VkFormat mColorformat = VK_FORMAT_B8G8R8A8_UNORM;
	// Depth buffer format
	// Depth format is selected during Vulkan initialization
	VkFormat mDepthFormat;
	// Command buffer pool
	VkCommandPool mCmdPool;
	// Command buffer used for setup
	VkCommandBuffer setupCmdBuffer = VK_NULL_HANDLE;
	/** @brief Pipeline stages used to wait at for graphics queue submissions */
	VkPipelineStageFlags submitPipelineStages = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	// Contains command buffers and semaphores to be presented to the queue
	VkSubmitInfo mSubmitInfo;
	// Command buffers used for rendering
	std::vector<VkCommandBuffer> mDrawCmdBuffers;
	// Global render pass for frame buffer writes
	VkRenderPass mRenderPass;
	// List of available frame buffers (same as number of swap chain images)
	std::vector<VkFramebuffer>mFrameBuffers;
	// Active frame buffer index
	//uint32_t mCurrentBuffer = 0;
	// Descriptor set pool
	VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
	// List of shader modules created (stored for cleanup)
	std::vector<VkShaderModule> shaderModules;
	// Pipeline cache object
	VkPipelineCache pipelineCache;
	// Wraps the swap chain to present images (framebuffers) to the windowing system
	//VulkanSwapChain gSwapChain;
	// Synchronization semaphores
	struct
	{
		// Swap chain image presentation
		VkSemaphore presentComplete;
		// Command buffer submission and execution
		VkSemaphore renderComplete;
		// Text overlay submission and execution
		VkSemaphore textOverlayComplete;
	} semaphores;
	// Simple texture loader
	vkTools::VulkanTextureLoader *textureLoader = nullptr;
	// Returns the base asset path (for shaders, models, textures) depending on the os
	
public: 
	bool prepared = false;
	uint32_t width = 1280;
	uint32_t height = 720;

	VkClearColorValue defaultClearColor = { { 0.025f, 0.025f, 0.025f, 1.0f } };

	float mZoom = 0;

	static std::vector<const char*> args;

	// Defines a frame rate independent timer value clamped from -1.0...1.0
	// For use in animations, rotations, etc.
	float timer = 0.0f;
	// Multiplier for speeding up (or slowing down) the global timer
	float timerSpeed = 0.25f;
	
	bool paused = false;

	bool mEnableTextOverlay = true;
	VulkanTextOverlay *mTextOverlay;

	// Use to adjust mouse rotation speed
	float rotationSpeed = 0.5f;
	// Use to adjust mouse zoom speed
	float zoomSpeed = 1.0f;

	VkCamera mCamera;

	Vector3 mRotation;
	Vector3 cameraPos;
	Vector2 mousePos;

	std::string title = "VkCore";
	std::string name = "VkCore";

	struct 
	{
		VkImage image;
		VkDeviceMemory mem;
		VkImageView view;
	} depthStencil;

	// Gamepad state (only one pad supported)
	struct
	{
		glm::vec2 axisLeft = glm::vec2(0.0f);
		glm::vec2 axisRight = glm::vec2(0.0f);
	} gamePadState;

	// OS specific 
#if defined(_WIN32)
	HWND mHwndWinow;
	HINSTANCE mWindowInstance;
#elif defined(__ANDROID__)
	android_app* androidApp;
	// true if application has focused, false if moved to background
	bool focused = false;
#elif defined(__linux__)
	struct {
		bool left = false;
		bool right = false;
		bool middle = false;
	} mouseButtons;
	bool quit = false;
	xcb_connection_t *connection;
	xcb_screen_t *screen;
	xcb_window_t mHwndWinow;
	xcb_intern_atom_reply_t *atom_wm_delete_window;
#endif

	VulkanBase(bool enableValidation, PFN_GetEnabledFeatures enabledFeaturesFn = nullptr);

	~VulkanBase();

	// Setup the vulkan instance, enable required extensions and connect to the physical device (GPU)
	void initVulkan(bool enableValidation);

	// Called if the window is resized and some resources have to be recreatesd
	void windowResize();
	VkResult createInstance(bool enableValidation);
	std::string getWindowTitle();
	const std::string getAssetPath();

#if defined(_WIN32)
	void setupConsole(std::string title);
	HWND setupWindow(HINSTANCE hinstance, WNDPROC wndproc);
	void handleMessages(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
#elif defined(__ANDROID__)
	static int32_t handleAppInput(struct android_app* app, AInputEvent* event);
	static void handleAppCommand(android_app* app, int32_t cmd);
#elif defined(__linux__)
	xcb_window_t setupWindow();
	void initxcbConnection();
	void handleEvent(const xcb_generic_event_t *event);
#endif
	virtual void render() = 0;

	virtual void viewChanged();

	virtual void keyPressed(uint32_t keyCode);

	virtual void windowResized();
	// Pure virtual function to be overriden by the dervice class
	// Called in case of an event where e.g. the framebuffer has to be rebuild and thus
	// all command buffers that may reference this
	virtual void buildCommandBuffers();

	// Creates a new (graphics) command pool object storing command buffers
	void createCommandPool();
	// Setup default depth and stencil views
	virtual void setupDepthStencil();
	// Create framebuffers for all requested swap chain images
	// Can be overriden in derived class to setup a custom framebuffer (e.g. for MSAA)
	virtual void setupFrameBuffer();
	// Setup a default render pass
	// Can be overriden in derived class to setup a custom render pass (e.g. for MSAA)
	virtual void setupRenderPass();

	// Connect and prepare the swap chain
	void initSwapchain();
	// Create swap chain images
	void setupSwapChain();

	// Check if command buffers are valid (!= VK_NULL_HANDLE)
	bool checkCommandBuffers();
	// Create command buffers for drawing commands
	void createCommandBuffers();
	// Destroy all command buffers and set their handles to VK_NULL_HANDLE
	// May be necessary during runtime if options are toggled 
	void destroyCommandBuffers();
	// Create command buffer for setup commands
	void createSetupCommandBuffer();
	// Finalize setup command bufferm submit it to the queue and remove it
	void flushSetupCommandBuffer();

	// Command buffer creation
	// Creates and returns a new command buffer
	VkCommandBuffer createCommandBuffer(VkCommandBufferLevel level, bool begin);
	// End the command buffer, submit it to the queue and free (if requested)
	// Note : Waits for the queue to become idle
	void flushCommandBuffer(VkCommandBuffer commandBuffer, VkQueue queue, bool free);

	// Create a cache pool for rendering pipelines
	void createPipelineCache();

	// Prepare commonly used Vulkan functions
	virtual void prepare();

	// Load a SPIR-V shader
	VkPipelineShaderStageCreateInfo loadShader(std::string fileName, VkShaderStageFlagBits stage);
	VkPipelineShaderStageCreateInfo loadShaderGLSL(std::string fileName, VkShaderStageFlagBits stage);
	// Create a buffer, fill it with data (if != NULL) and bind buffer memory
	VkBool32 createBuffer(
		VkBufferUsageFlags usageFlags,
		VkMemoryPropertyFlags memoryPropertyFlags,
		VkDeviceSize size,
		void *data,
		VkBuffer *buffer,
		VkDeviceMemory *memory);
	// This version always uses HOST_VISIBLE memory
	VkBool32 createBuffer(
		VkBufferUsageFlags usage,
		VkDeviceSize size,
		void *data,
		VkBuffer *buffer,
		VkDeviceMemory *memory);
	// Overload that assigns buffer info to descriptor
	VkBool32 createBuffer(
		VkBufferUsageFlags usage,
		VkDeviceSize size,
		void *data,
		VkBuffer *buffer,
		VkDeviceMemory *memory,
		VkDescriptorBufferInfo *descriptor);
	// Overload to pass memory property flags
	VkBool32 createBuffer(
		VkBufferUsageFlags usage,
		VkMemoryPropertyFlags memoryPropertyFlags,
		VkDeviceSize size,
		void *data,
		VkBuffer *buffer,
		VkDeviceMemory *memory,
		VkDescriptorBufferInfo *descriptor);

	// Load a mesh (using ASSIMP) and create vulkan vertex and index buffers with given vertex layout
	void loadMesh(
		std::string fiename, 
		vkMeshLoader::MeshBuffer *meshBuffer, 
		std::vector<vkMeshLoader::VertexLayout> vertexLayout, 
		float scale);
	void loadMesh(
		std::string filename, 
		vkMeshLoader::MeshBuffer *meshBuffer, 
		std::vector<vkMeshLoader::VertexLayout> 
		vertexLayout, 
		vkMeshLoader::MeshCreateInfo *meshCreateInfo);

	// Start the main render loop
	void renderLoop();

	void updateTextOverlay();

	// Called when the text overlay is updating
	// Can be overriden in derived class to add custom text to the overlay
	virtual void getOverlayText(VulkanTextOverlay * textOverlay);

	// Prepare the frame for workload submission
	// - Acquires the next image from the swap chain 
	// - Sets the default wait and signal semaphores
	void prepareFrame();

	// Submit the frames' workload 
	// - Submits the text overlay (if enabled)
	void submitFrame();

};

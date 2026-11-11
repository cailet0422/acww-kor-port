/* The Vulkan functions for showing pictures in a window (the surface and swapchain
 * extensions): VK_FN(name) for each. An instance made without those extensions has none of
 * them, so they are looked up apart (vk_api_load_wsi). */
VK_FN(DestroySurfaceKHR)
VK_FN(GetPhysicalDeviceSurfaceSupportKHR)
VK_FN(GetPhysicalDeviceSurfaceCapabilitiesKHR)
VK_FN(GetPhysicalDeviceSurfaceFormatsKHR)
VK_FN(GetPhysicalDeviceSurfacePresentModesKHR)
VK_FN(CreateSwapchainKHR)
VK_FN(DestroySwapchainKHR)
VK_FN(GetSwapchainImagesKHR)
VK_FN(AcquireNextImageKHR)
VK_FN(QueuePresentKHR)

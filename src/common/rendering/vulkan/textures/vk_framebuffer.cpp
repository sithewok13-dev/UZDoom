/*
** vk_framebuffer.cpp
**
** Vulkan backend
**
**---------------------------------------------------------------------------
**
** Copyright 2017-2025 GZDoom Maintainers and Contributors
** Copyright 2025-2026 UZDoom Maintainers and Contributors
**
** SPDX-License-Identifier: GPL-3.0-or-later
**
**---------------------------------------------------------------------------
**
** Copyright 2016-2020 Magnus Norddahl
**
** SPDX-License-Identifier: Zlib
**
**---------------------------------------------------------------------------
**
*/

#include <zvulkan/vulkanobjects.h>
#include <zvulkan/vulkandevice.h>
#include <zvulkan/vulkanbuilders.h>
#include <zvulkan/vulkanswapchain.h>
#include "vulkan/system/vk_renderdevice.h"
#include "vulkan/renderer/vk_postprocess.h"
#include "vk_framebuffer.h"

CVAR(Bool, vk_hdr, false, CVAR_ARCHIVE | CVAR_GLOBALCONFIG);
CVAR(Bool, vk_exclusivefullscreen, false, CVAR_ARCHIVE | CVAR_GLOBALCONFIG);

VkFramebufferManager::VkFramebufferManager(VulkanRenderDevice* fb) : fb(fb)
{
	SwapChain = VulkanSwapChainBuilder()
		.Create(fb->device.get());

	SwapChainImageAvailableSemaphore = SemaphoreBuilder()
		.DebugName("SwapChainImageAvailableSemaphore")
		.Create(fb->device.get());
}

VkFramebufferManager::~VkFramebufferManager()
{
}

void VkFramebufferManager::AcquireImage()
{
	bool exclusiveFullscreen = fb->IsFullscreen() && vk_exclusivefullscreen;
	if (SwapChain->Lost() || fb->GetClientWidth() != CurrentWidth || fb->GetClientHeight() != CurrentHeight || fb->GetVSync() != CurrentVSync || CurrentHdr != vk_hdr || CurrentExclusiveFullscreen != exclusiveFullscreen)
	{
		Framebuffers.clear();

		CurrentWidth = fb->GetClientWidth();
		CurrentHeight = fb->GetClientHeight();
		CurrentVSync = fb->GetVSync();
		CurrentHdr = vk_hdr;
		CurrentExclusiveFullscreen = exclusiveFullscreen;

#ifdef DOOM_IOS
		// iOS: three images with vsync too, so a frame a little over a 120 Hz
		// screen's 8.3 ms doesn't drop the game to 60
		SwapChain->Create(CurrentWidth, CurrentHeight, 3, CurrentVSync, CurrentHdr, CurrentExclusiveFullscreen);
#else
		SwapChain->Create(CurrentWidth, CurrentHeight, CurrentVSync ? 2 : 3, CurrentVSync, CurrentHdr, CurrentExclusiveFullscreen);
#endif

		RenderFinishedSemaphores.clear();
		for (int i = 0; i < SwapChain->ImageCount(); i++)
		{
			RenderFinishedSemaphores.push_back(SemaphoreBuilder()
				.DebugName("RenderFinishedSemaphore")
				.Create(fb->device.get()));
		}
	}

	PresentImageIndex = SwapChain->AcquireImage(SwapChainImageAvailableSemaphore.get());
	if (PresentImageIndex != -1)
	{
		fb->GetPostprocess()->DrawPresentTexture(fb->mOutputLetterbox, true, false);
	}
}

#ifdef DOOM_IOS
// Added (iOS): the frames' pace (ios/i_ios.mm, ios/IOSPacer.h)
extern "C" void IOS_PresentRefresh(uint64_t refreshNs);
extern "C" void IOS_PresentShown(uint32_t id, uint64_t actualNs);
extern "C" uint64_t IOS_PresentPlan(uint32_t* pId);
#endif

void VkFramebufferManager::QueuePresent()
{
	if (PresentImageIndex != -1)
	{
#ifdef DOOM_IOS
		// Added (iOS): with VK_GOOGLE_display_timing, each frame asks to be
		// shown at the time the pace plans for it, and the times earlier
		// frames were really shown go back to the pacer (and the log)
		VkDevice dev = fb->device->device;
		VkSwapchainKHR sc = SwapChain->Handle();
		if (fb->device->SupportsExtension(VK_GOOGLE_DISPLAY_TIMING_EXTENSION_NAME) && sc != VK_NULL_HANDLE
			&& vkGetPastPresentationTimingGOOGLE && vkGetRefreshCycleDurationGOOGLE)
		{
			static VkSwapchainKHR refreshOf = VK_NULL_HANDLE;
			if (refreshOf != sc)
			{
				VkRefreshCycleDurationGOOGLE rc = {};
				if (vkGetRefreshCycleDurationGOOGLE(dev, sc, &rc) == VK_SUCCESS) IOS_PresentRefresh(rc.refreshDuration);
				refreshOf = sc;
			}
			for (int round = 0; round < 4; round++)
			{
				VkPastPresentationTimingGOOGLE past[16];
				uint32_t n = 16;
				const VkResult r = vkGetPastPresentationTimingGOOGLE(dev, sc, &n, past);
				if (r != VK_SUCCESS && r != VK_INCOMPLETE) break;
				for (uint32_t i = 0; i < n; i++) IOS_PresentShown(past[i].presentID, past[i].actualPresentTime);
				if (r == VK_SUCCESS) break;
			}
			VkPresentTimeGOOGLE when = {};
			when.desiredPresentTime = IOS_PresentPlan(&when.presentID);
			VkPresentTimesInfoGOOGLE times = {};
			times.sType = VK_STRUCTURE_TYPE_PRESENT_TIMES_INFO_GOOGLE;
			times.swapchainCount = 1;
			times.pTimes = &when;
			SwapChain->QueuePresent(PresentImageIndex, RenderFinishedSemaphores[PresentImageIndex].get(), &times);
			return;
		}
#endif
		SwapChain->QueuePresent(PresentImageIndex, RenderFinishedSemaphores[PresentImageIndex].get());
	}
}

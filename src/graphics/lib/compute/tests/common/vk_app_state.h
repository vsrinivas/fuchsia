// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_COMPUTE_TESTS_COMMON_VK_APP_STATE_H_
#define SRC_GRAPHICS_LIB_COMPUTE_TESTS_COMMON_VK_APP_STATE_H_

//
//
//

#include <stdbool.h>
#include <vulkan/vulkan_core.h>

//
//
//

#ifdef __cplusplus
extern "C" {
#endif

//
//
//

// Helper routines to initialize a Vulkan instance and device based on
// application preferences in the most simple, though flexible way.
//
// Usage example:
//
//   1) Setup a vk_app_state_config_t struct with appropriate configuration
//      data (see below for details), then call vk_app_state_init() to
//      initialize a vk_app_state_t instance.
//
//      This creates a single VkInstance and single VkDevice according to
//      the configuration data.
//
//   2) Use the Vulkan handles / queue families returned by the vk_app_state_t
//      to do your work then call vk_app_state_destroy() when everything is done.
//
//   3) If |require_swapchain| was set when creating the instance, one can
//      also call vk_app_state_create_surface() to create a presenstation
//      surface easily. See vk_swapchain.h to see how one can use it to
//      manage a Vulkan swapchain.
//
//   4) vk_app_state_poll_events() can also be used to poll for user events
//      during the application's main event loop. Note that event handling
//      is essentially missing at the moment: the function returns false
//      only when the user closes the display window, no mouse or key event
//      handling is possible for now.
//
//      One can create its own surface with a different library (e.g. glfw)
//      in order to perform better event handling. This function is only
//      provided as a convenience to write very simple user-facing tests
//      and demos.

// Device features and extensions configurations is performed through
// distinct mechanisms in order to simplify client code:
//
//  - Some features or extensions have a dedicated named flag in
//    |vk_app_state_config_t| in order to enable them _if_ they are available.
//
//    This is very convenient because all probing is performed directly by
//    vk_app_state_init(), and availability can later be checked with a
//    corresponding boolean flag in |vk_app_state_t|.
//
//    For example, see |vk_app_state_config_t::enable_debug_report| and the
//    corresponding |vk_app_state_t::has_debug_report| flag.
//
//  - The first physical device is selected by default, but this can be
//    overriden by setting |physical_device| in a |vk_device_config_t| struct,
//    or by setting |vendor_id| and |device_id| instead.
//
//  - It is possible to provide a callback that will be called by
//    vk_app_state_init() in order to let the application probe the physical
//    devices after the VkInstance is created. See |device_config_callback|
//    below. If the callback pointer is NULL, the values from |device_config|
//    are used directly instead, to simplify client code.
//

// Maximum number of device extensions to be listed in |vk_device_config_t|.
#define VK_DEVICE_CONFIG_MAX_EXTENSIONS 16

// A small struct describing parameters that determine which Vulkan device
// to create, which which extensions, during vk_app_state_init().
typedef struct vk_device_config_t
{
  // If this is not VK_NULL_HANDLE, force the use of this specific GPU
  // to create a VkDevice. This also that means the xxx_id fields below are
  // will be ignored.
  VkPhysicalDevice physical_device;

  // Vulkan vendor and device ID. May be useful to select a GPU based on
  // its vendor ID (or vendor + device IDs) instead if there are several
  // installed on the current device.

  // NOTE: If none of |vendor_id|, |device_id| and |physical_device| are
  // set, the default is to use the first listed device.

  uint32_t vendor_id;  // 0, or a Vulkan vendor ID.
  uint32_t device_id;  // 0, or a Vulkan device ID. Ignored if |vendor_id| is 0.

  // If not 0, the device should provide a single queue family that provides
  // all queue flags at the same time.
  // NOTE: At the moment, this only supports VK_QUEUE_GRAPHICS_BIT and VK_QUEUE_COMPUTE_BIT.
  VkQueueFlags required_combined_queues;

  // If not 0, the device should provide single queue families that support all bits
  // in this bitmask. Only bits that are not already in |required_combined_queues|
  // will actually be tested here.
  // NOTE: At the moment, this only supports VK_QUEUE_GRAPHICS_BIT and VK_QUEUE_COMPUTE_BIT.
  VkQueueFlags required_queues;  // Set of required queue family flags;

  // Specify a list of required extensions to enable for the device.
  uint32_t     extensions_count;
  const char * extensions_names[VK_DEVICE_CONFIG_MAX_EXTENSIONS];

  // The list of required features to be supported by the device.
  VkPhysicalDeviceFeatures features;

} vk_device_config_t;

// Pointer to function that will be called once per physical device just after
// the VkInstance creation, and that will need to fill a vk_device_config_t
// for it.
//
// |opaque| is a user-provided opaque pointer. |instance| is the VkInstance
// and |physical_device| is a given physical device handle.
//
// On success, should return true and set |*device_config| appropriately.
// Note that this stops the iteration.
//
// Otherwise, return false to indicate the physical device should be ignored.
typedef bool (*vk_device_config_callback_t)(void *               opaque,
                                            VkInstance           instance,
                                            VkPhysicalDevice     physical_device,
                                            vk_device_config_t * device_config);

// Structure containing configuration information for a vk_app_state_t
// instance creation.
typedef struct vk_app_state_config_t
{
  const char * app_name;     // Optional application name.
  const char * engine_name;  // Optional engine name.

  bool enable_validation;      // True to enable validation layers.
  bool enable_pipeline_cache;  // True to enable on-disk pipeline cache.

  // The following enable_xxx flags are provided as a convenience to enable
  // specific extensions if they are available only. I.e. vk_app_state_init()
  // will do the probing and enabling automatically for all flags set to true.
  // Availability can later be tested by looking at |has_xxx| flags in
  // |vk_app_state_t|.

  // Having dedicated named flags for these greatly simplifies client code,
  // since it avoids the need for a custom |device_config_callback| to see
  // if the extensions are listed.

  bool enable_debug_report;  // True to enable debug report callbacks if available.
  // TODO(digit): Change this to |enable_debug| and support debug_utils if available.

  bool enable_tracing;  // True to enable tracing support.

  bool enable_amd_statistics;         // True to enable VK_AMD_shader_info if available.
  bool enable_subgroup_size_control;  // True to enable VK_EXT_subgroup_size_control if available.

  // A callback and associated opaque pointer. If not NULL, the function is
  // called by vk_app_state_init() in order to return device configuration data
  // from the current VkInstance handle. This allows clients to probe the
  // instance and select the exact device and/or extensions they need.
  //
  // If NULL, then the content of |device_config| is used directly instead to
  // simplify client code.
  vk_device_config_callback_t device_config_callback;
  void *                      device_config_opaque;

  // A vk_device_config_t describing required device configuration. Only used
  // as a convenience if |device_config_callback| is NULL.
  vk_device_config_t device_config;

  bool require_swapchain;  // True if swapchain support is required.

  bool disable_swapchain_present;  // True to disable swapchain presentation.
  // This may not work on all platforms, and should only be used for benchmarking!
  // NOTE: This is experimental, do not rely on this in the future!

} vk_app_state_config_t;

// Simple struct to gather application-specific Vulkan state for our test
// programs. Usage is:
//   1) Use vk_app_state_init() to initialize instance.
//   2) Use the handles and structs provided to perform Vulkan operations.
//   3) Call vk_app_state_destroy() to release all Vulkan resources.
typedef struct
{
  VkInstance                       instance;
  const VkAllocationCallbacks *    ac;
  VkDevice                         d;
  VkPipelineCache                  pc;
  VkPhysicalDevice                 pd;
  VkPhysicalDeviceProperties       pdp;
  VkPhysicalDeviceMemoryProperties pdmp;
  uint32_t                         qfi;          // queue family index
  uint32_t                         compute_qfi;  // UINT32_MAX if not available.

  // Report available optional extensions that were part of the configuration.
  bool has_debug_report;
  bool has_amd_statistics;
  bool has_subgroup_size_control;

  // internal implementation details.
  uintptr_t internal_storage[512];

} vk_app_state_t;

// Initialize vk_app_state_t instance. |vendor_id| and |device_id| should
// be pointers to uint32_t values which can be either 0 or a specific Vulkan
// vendor or device ID. If both are zero, the first device will be selected.
// If both are non-zero, only this specific device will be selected. Otherwise
// results are undefined.
//
// Returns true on success, false on failure (after printing error message to
// stderr).
extern bool
vk_app_state_init(vk_app_state_t * app_state, const vk_app_state_config_t * app_state_config);

// Destroy a vk_app_state_t instance.
extern void
vk_app_state_destroy(vk_app_state_t * app_state);

// Dump state of a vk_app_state_t to stdout for debugging.
extern void
vk_app_state_print(const vk_app_state_t * app_state);

// A small structure to list the indices of queue families initialized/used
// by a given vk_app_instance_t.
#define MAX_VK_QUEUE_FAMILIES 2

typedef struct
{
  uint32_t count;
  uint32_t indices[MAX_VK_QUEUE_FAMILIES];
} vk_queue_families_t;

// Return the number of queue families this instance supports.
extern vk_queue_families_t
vk_app_state_get_queue_families(const vk_app_state_t * app_state);

// Create a presentation surface. |window_width| and |window_height| will be
// ignored if presentation happens on the framebuffer. This requires that
// |enable_swapchain| was set to true when creating the vk_app_state_t
// instance.
extern VkSurfaceKHR
vk_app_state_create_surface(const vk_app_state_t * app_state,
                            uint32_t               window_width,
                            uint32_t               window_height);

// Poll UI events, return true on success, false if the program should quit.
// This function should be called before any frame draw call.
extern bool
vk_app_state_poll_events(vk_app_state_t * app_state);

//
//
//

#ifdef __cplusplus
}
#endif

//
//
//

#endif  // SRC_GRAPHICS_LIB_COMPUTE_TESTS_COMMON_VK_APP_STATE_H_

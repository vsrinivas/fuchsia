// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//
//
//

#include "vk_app_state.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "utils.h"
#include "vk_strings.h"
#include "vk_utils.h"

#if VK_USE_PLATFORM_FUCHSIA
#include <lib/async-loop/default.h>
#include <lib/async-loop/loop.h>
#include <trace-provider/provider.h>
#endif

// For now, using GLFW to create a presentation surface on the host.
#if !VK_USE_PLATFORM_FUCHSIA
#include <GLFW/glfw3.h>

// Print GLFW errors to stderr to ease debugging.
static void
glfw_error_callback(int error, const char * message)
{
  fprintf(stderr, "GLFW:error=%d: %s\n", error, message);
}

static GLFWwindow * glfw_window;

static uint32_t glfw_window_width = 1024, glfw_window_height = 1024;

static void
glfw_setup_config(uint32_t window_width, uint32_t window_height)
{
  if (window_width > 0)
    glfw_window_width = window_width;
  if (window_height > 0)
    glfw_window_height = window_height;
}

static GLFWwindow *
glfw_get_window()
{
  if (!glfw_window)
    {
      glfwInit();

      glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
      //glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

      glfwSetErrorCallback(glfw_error_callback);
      glfw_window =
        glfwCreateWindow(glfw_window_width, glfw_window_height, "Spinel Demo Test", NULL, NULL);
      ASSERT_MSG(glfw_window, "Could not create GLFW presentation window!");
    }
  return glfw_window;
}

static bool
glfw_poll_events()
{
  GLFWwindow * window = glfw_get_window();

  if (!window)
    return false;

  if (glfwWindowShouldClose(window))
    return false;

  glfwPollEvents();
  return true;
}

#endif  // !VK_USE_PLATFORM_FUCHSIA

//
// Generic const char * vector. Used for extensions and layout names.
//

typedef struct
{
  uint32_t      count;
  uint32_t      capacity;
  const char ** items;
} StringList;

#define STRING_LIST_INITIALIZER                                                                    \
  {                                                                                                \
  }

void
string_list_append(StringList * list, const char * value)
{
  ASSERT_MSG(value != NULL, "Invalid NULL string value.\n");
  if (list->count == list->capacity)
    {
      uint32_t      new_capacity = list->capacity + (list->capacity >> 1) + 4;
      const char ** new_items    = realloc(list->items, new_capacity * sizeof(list->items[0]));
      ASSERT_MSG(new_items != NULL, "Out of memory.\n");
      list->capacity = new_capacity;
      list->items    = new_items;
    }
  list->items[list->count++] = value;
}

bool
string_list_contains(const StringList * list, const char * value)
{
  ASSERT_MSG(value != NULL, "Invalid NULL string value.\n");
  for (uint32_t nn = 0; nn < list->count; ++nn)
    {
      if (!strcmp(list->items[nn], value))
        return true;
    }
  return false;
}

void
string_list_add(StringList * list, const char * value)
{
  ASSERT_MSG(value != NULL, "Invalid NULL string value.\n");
  if (!string_list_contains(list, value))
    string_list_append(list, value);
}

void
string_list_add_n(StringList * list, uint32_t count, const char * const * values)
{
  for (uint32_t nn = 0; nn < count; ++nn)
    string_list_add(list, values[nn]);
}

void
string_list_free(StringList * list)
{
  if (list->capacity > 0)
    {
      free(list->items);
      list->items    = NULL;
      list->capacity = 0u;
      list->count    = 0u;
    }
}

//
// Instance specific info
//

typedef struct
{
  uint32_t            count;
  VkLayerProperties * layers;
} LayersList;

static void
layers_list_init(LayersList * list)
{
  vk(EnumerateInstanceLayerProperties(&list->count, NULL));
  list->layers = calloc(list->count, sizeof(list->layers[0]));
  vk(EnumerateInstanceLayerProperties(&list->count, list->layers));
}

static bool
layers_list_contains(const LayersList * list, const char * name)
{
  for (uint32_t nn = 0; nn < list->count; ++nn)
    {
      if (!strcmp(list->layers[nn].layerName, name))
        return true;
    }
  return false;
}

static void
layers_list_destroy(LayersList * list)
{
  if (list->count)
    {
      free(list->layers);
      list->layers = NULL;
      list->count  = 0;
    }
}

typedef struct
{
  uint32_t                count;
  VkExtensionProperties * extensions;
} ExtensionsList;

static void
extensions_list_init(ExtensionsList * list, const char * layer_name)
{
  vk(EnumerateInstanceExtensionProperties(layer_name, &list->count, NULL));
  if (list->count)
    {
      list->extensions = calloc(list->count, sizeof(list->extensions[0]));
      vk(EnumerateInstanceExtensionProperties(layer_name, &list->count, list->extensions));
    }
}

static bool
extensions_list_contains(const ExtensionsList * list, const char * name)
{
  for (uint32_t nn = 0; nn < list->count; ++nn)
    {
      if (!strcmp(list->extensions[nn].extensionName, name))
        return true;
    }
  return false;
}

static void
extensions_list_destroy(ExtensionsList * list)
{
  if (list->count)
    {
      free(list->extensions);
      list->extensions = NULL;
      list->count      = 0;
    }
}

typedef struct
{
  LayersList       layers;
  ExtensionsList   extensions;
  ExtensionsList * layer_extensions;

} InstanceInfo;

static InstanceInfo
instance_info_create(void)
{
  InstanceInfo info = {};

  layers_list_init(&info.layers);
  extensions_list_init(&info.extensions, NULL);
  info.layer_extensions = calloc(info.layers.count, sizeof(info.layer_extensions[0]));
  for (uint32_t nn = 0; nn < info.layers.count; ++nn)
    extensions_list_init(&info.layer_extensions[nn], info.layers.layers[nn].layerName);

  return info;
}

static void
instance_info_destroy(InstanceInfo * info)
{
  if (info->layers.count > 0)
    {
      for (uint32_t nn = 0; nn < info->layers.count; ++nn)
        extensions_list_destroy(&info->layer_extensions[nn]);

      free(info->layer_extensions);
      info->layer_extensions = NULL;
    }

  layers_list_destroy(&info->layers);
  extensions_list_destroy(&info->extensions);
}

static bool
instance_info_has_layer(const InstanceInfo * info, const char * layer_name)
{
  return layers_list_contains(&info->layers, layer_name);
}

static bool
instance_info_has_extension(const InstanceInfo * info, const char * extension_name)
{
  if (extensions_list_contains(&info->extensions, extension_name))
    return true;

  for (uint32_t nn = 0; nn < info->layers.count; ++nn)
    {
      if (extensions_list_contains(&info->layer_extensions[nn], extension_name))
        return true;
    }
  return false;
}

static bool
instance_info_validate_layers_and_extensions(const InstanceInfo * info,
                                             const StringList *   layers,
                                             const StringList *   extensions)
{
  bool success = true;

  // Check layers.
  for (uint32_t nn = 0; nn < layers->count; ++nn)
    {
      const char * name = layers->items[nn];
      if (!instance_info_has_layer(info, name))
        {
          fprintf(stderr, "Missing Vulkan layer: %s\n", name);
          success = false;
        }
    }

  // Check extensions.
  for (uint32_t nn = 0; nn < extensions->count; ++nn)
    {
      const char * name = extensions->items[nn];
      if (!instance_info_has_extension(info, name))
        {
          fprintf(stderr, "Missing Vulkan extensions: %s\n", name);
          success = false;
        }
    }
  return success;
}

static void
instance_info_print(const InstanceInfo * info)
{
  printf("Instance info:\n");
  for (uint32_t n = 0; n < info->layers.count; ++n)
    {
      printf("  layer %s (spec version %u)\n",
             info->layers.layers[n].layerName,
             info->layers.layers[n].specVersion);
    }

  for (uint32_t n = 0; n < info->extensions.count; ++n)
    {
      printf("  extension %s (spec version %u)\n",
             info->extensions.extensions[n].extensionName,
             info->extensions.extensions[n].specVersion);
    }

  for (uint32_t n = 0; n < info->layers.count; ++n)
    {
      const char *           layer_name = info->layers.layers[n].layerName;
      const ExtensionsList * list       = &info->layer_extensions[n];
      for (uint32_t m = 0; m < list->count; ++m)
        {
          printf("  layer(%s) extension %s (spec version %u)\n",
                 layer_name,
                 list->extensions[m].extensionName,
                 list->extensions[m].specVersion);
        }
    }
}

//
// Physical device list.
//

typedef struct
{
  uint32_t           count;
  VkPhysicalDevice * devices;
} GpuList;

static void
gpu_list_init(GpuList * list, VkInstance instance)
{
  list->count   = 0;
  list->devices = NULL;

  vk(EnumeratePhysicalDevices(instance, &list->count, NULL));
  if (list->count > 0)
    {
      list->devices = malloc(list->count * sizeof(list->devices[0]));
      vk(EnumeratePhysicalDevices(instance, &list->count, list->devices));
    }
}

static void
gpu_list_destroy(GpuList * list)
{
  if (list->count)
    {
      free(list->devices);
      list->devices = NULL;
      list->count   = 0;
    }
}

//
// Device specific info
//

typedef struct
{
  uint32_t                extensions_count;
  VkExtensionProperties * extensions;

} DeviceInfo;

static DeviceInfo
device_info_create(VkInstance instance, VkPhysicalDevice physical_device)
{
  GET_VULKAN_INSTANCE_PROC_ADDR(vkEnumerateDeviceExtensionProperties);

  DeviceInfo info = {};

  vk(EnumerateDeviceExtensionProperties(physical_device, NULL, &info.extensions_count, NULL));
  info.extensions = calloc(info.extensions_count, sizeof(info.extensions[0]));
  vk(EnumerateDeviceExtensionProperties(physical_device,
                                        NULL,
                                        &info.extensions_count,
                                        info.extensions));

  return info;
}

static bool
device_info_has_extension(const DeviceInfo * info, const char * extension_name)
{
  for (uint32_t nn = 0; nn < info->extensions_count; ++nn)
    {
      if (!strcmp(info->extensions[nn].extensionName, extension_name))
        return true;
    }
  return false;
}

static void
device_info_destroy(DeviceInfo * info)
{
  if (info->extensions_count > 0)
    {
      free(info->extensions);
      info->extensions       = NULL;
      info->extensions_count = 0;
    }
}

//
// Platform-specific surface creation code.
//

VkResult
create_surface_khr(VkInstance                    instance,
                   const VkAllocationCallbacks * ac,
                   VkSurfaceKHR *                surface_khr)
{
#define INSTANCE_LOADER(name) vkGetInstanceProcAddr(instance, name)

#if VK_USE_PLATFORM_FUCHSIA
  GET_VULKAN_INSTANCE_PROC_ADDR(vkCreateImagePipeSurfaceFUCHSIA);
  ASSERT_MSG(vkCreateImagePipeSurfaceFUCHSIA, "Missing %s!\n", "vkCreateImagePipeSurfaceFUCHSIA");

  VkImagePipeSurfaceCreateInfoFUCHSIA const surface_info = {
    .sType = VK_STRUCTURE_TYPE_IMAGEPIPE_SURFACE_CREATE_INFO_FUCHSIA,
    .pNext = NULL,
  };
  return vkCreateImagePipeSurfaceFUCHSIA(instance, &surface_info, ac, surface_khr);
#else   // !FUCHSIA
  GLFWwindow * window = glfw_get_window();
  return glfwCreateWindowSurface(instance, window, ac, surface_khr);
#endif  // !FUCHSIA
}

static bool
physical_device_supports_presentation(VkPhysicalDevice physical_device,
                                      uint32_t         queue_family_index,
                                      VkInstance       instance)
{
#if VK_USE_PLATFORM_FUCHSIA
  return true;
#else
  return glfwGetPhysicalDevicePresentationSupport(instance, physical_device, queue_family_index) ==
         GLFW_TRUE;
#endif
}

VkSurfaceKHR
vk_app_state_create_surface(const vk_app_state_t * app_state,
                            uint32_t               window_width,
                            uint32_t               window_height)
{
  VkSurfaceKHR surface;
#if !VK_USE_PLATFORM_FUCHSIA
  glfw_setup_config(window_width, window_height);
#endif
  VkResult result = create_surface_khr(app_state->instance, app_state->ac, &surface);
  VK_CHECK_MSG(result, "Could not create presentation surface");
  return surface;
}

//
// Pipeline cache support.
//

#ifdef __Fuchsia__
#define PIPELINE_CACHE_FILE_PATH "/cache/.vk_cache"
#else
#define PIPELINE_CACHE_FILE_PATH "/tmp/vk_app_pipeline_cache"
#endif

// Load file from |file_path| into a new heap block. On success return true
// and sets |*p_data| and |*p_data_size| accordingly. Caller must call free()
// to release the block later. On failure, simply return false.
static bool
file_read(const char * file_path, void ** p_data, size_t * p_data_size)
{
  FILE * f = fopen(file_path, "rb");
  if (!f)
    return false;

  bool success = false;
  if (fseek(f, 0, SEEK_END) != 0)
    goto EXIT;

  const size_t size = (size_t)ftell(f);
  if (size == 0)
    {
      *p_data      = NULL;
      *p_data_size = size;
      success      = true;
      goto EXIT;
    }

  void * data = malloc(size);
  if (!data)
    goto EXIT;

  if (fread(data, size, 1, f) != 1)
    {
      free(data);
      goto EXIT;
    }

  *p_data      = data;
  *p_data_size = size;
  success      = true;

EXIT:
  fclose(f);
  return success;
}

// Write |data_size| bytes from |data| into a file at |file_path|.
// On success, return true.
static bool
file_write(const char * file_path, const void * data, size_t data_size)
{
  FILE * f = fopen(file_path, "wb");
  if (!f)
    return false;

  bool success = fwrite(data, data_size, 1, f) == 1;
  fclose(f);

  return success;
}

// Try to load the pipeline cache data from |file_path| and return the
// corresponding VkPipelineCache handle, or VK_NULL_HANDLE if there is no
// file or if it could not be read of loaded properly.
static VkPipelineCache
pipeline_cache_load(const char *                  file_path,
                    VkDevice                      device,
                    const VkAllocationCallbacks * allocator)
{
  void * data      = NULL;
  size_t data_size = 0;

  // Ignore file read errors to create an empty cache in case of failure.
  (void)file_read(file_path, &data, &data_size);

  VkPipelineCache pipeline_cache;
  VkResult        vk_res = vkCreatePipelineCache(device,
                                          &(const VkPipelineCacheCreateInfo){
                                            .sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO,
                                            .initialDataSize = data_size,
                                            .pInitialData    = data,
                                          },
                                          allocator,
                                          &pipeline_cache);

  free(data);
  return (vk_res == VK_SUCCESS) ? pipeline_cache : VK_NULL_HANDLE;
}

// Try to save a pipeline cache into |file_path|. Return true on success,
// and false on failure. Note that if the handle is VK_NULL_HANDLE, this
// does not do anything and returns true.
static bool
pipeline_cache_save(VkPipelineCache               pipeline_cache,
                    const char *                  file_path,
                    VkDevice                      device,
                    const VkAllocationCallbacks * allocator)
{
  if (pipeline_cache == VK_NULL_HANDLE)
    return true;

  size_t   data_size = 0;
  VkResult res       = vkGetPipelineCacheData(device, pipeline_cache, &data_size, NULL);
  if (res != VK_SUCCESS)
    return false;

  bool success = false;

  void * data = NULL;
  if (!data_size)
    {
      success = true;
      goto EXIT;
    }

  data = malloc(data_size);
  if (!data)
    goto EXIT;

  res = vkGetPipelineCacheData(device, pipeline_cache, &data_size, data);
  if (res != VK_SUCCESS)
    {
      goto EXIT;
    }

  success = file_write(file_path, data, data_size);

EXIT:
  free(data);
  vkDestroyPipelineCache(device, pipeline_cache, allocator);
  return success;
}

//
// RenderDoc capture support.
//

#if !VK_USE_PLATFORM_FUCHSIA
#define USE_RENDERDOC_CAPTURE 1
#endif

#if USE_RENDERDOC_CAPTURE

#include <dlfcn.h>

typedef void (*pRENDERDOC_GetAPIVersion)(int * major, int * minor, int * patch);

// Use this to convert a RenderDoc pointer type to void*
#define IGNORED_PTR(type) void *

typedef enum
{
  eRENDERDOC_API_Version_1_1_2 = 10102,
} RENDERDOC_Version;

typedef void (*pRENDERDOC_StartFrameCapture)(void * device, void * wndHandle);

typedef uint32_t (*pRENDERDOC_EndFrameCapture)(void * device, void * wndHandle);

// eRENDERDOC_API_Version_1_1_2
typedef struct
{
  pRENDERDOC_GetAPIVersion GetAPIVersion;

  IGNORED_PTR(pRENDERDOC_SetCaptureOptionU32) SetCaptureOptionU32;
  IGNORED_PTR(pRENDERDOC_SetCaptureOptionF32) SetCaptureOptionF32;

  IGNORED_PTR(pRENDERDOC_GetCaptureOptionU32) GetCaptureOptionU32;
  IGNORED_PTR(pRENDERDOC_GetCaptureOptionF32) GetCaptureOptionF32;

  IGNORED_PTR(pRENDERDOC_SetFocusToggleKeys) SetFocusToggleKeys;
  IGNORED_PTR(pRENDERDOC_SetCaptureKeys) SetCaptureKeys;

  IGNORED_PTR(pRENDERDOC_GetOverlayBits) GetOverlayBits;
  IGNORED_PTR(pRENDERDOC_MaskOverlayBits) MaskOverlayBits;

  IGNORED_PTR(pRENDERDOC_Shutdown) Shutdown;
  IGNORED_PTR(pRENDERDOC_UnloadCrashHandler) UnloadCrashHandler;

  IGNORED_PTR(pRENDERDOC_SetCaptureFilePathTemplate) SetCaptureFilePathTemplate;
  IGNORED_PTR(pRENDERDOC_GetCaptureFilePathTemplate) GetCaptureFilePathTemplate;

  IGNORED_PTR(pRENDERDOC_GetNumCaptures) GetNumCaptures;
  IGNORED_PTR(pRENDERDOC_GetCapture) GetCapture;

  IGNORED_PTR(pRENDERDOC_TriggerCapture) TriggerCapture;

  IGNORED_PTR(pRENDERDOC_IsTargetControlConnected) IsTargetControlConnected;
  IGNORED_PTR(pRENDERDOC_LaunchReplayUI) LaunchReplayUI;

  IGNORED_PTR(pRENDERDOC_SetActiveWindow) SetActiveWindow;

  pRENDERDOC_StartFrameCapture StartFrameCapture;

  IGNORED_PTR(pRENDERDOC_IsFrameCapturing) IsFrameCapturing;

  pRENDERDOC_EndFrameCapture EndFrameCapture;

  IGNORED_PTR(pRENDERDOC_TriggerMultiFrameCapture) TriggerMultiFrameCapture;
} RENDERDOC_API_1_1_2;

#undef IGNORED_PTR

typedef int (*pRENDERDOC_GetAPI)(RENDERDOC_Version version, void ** outAPIPointers);

static RENDERDOC_API_1_1_2 * s_renderdoc_api = NULL;
static void *                s_renderdoc_lib = NULL;

static void
renderdoc_capture_setup(bool debug)
{
  s_renderdoc_lib = dlopen("librenderdoc.so", RTLD_NOW | RTLD_NOLOAD);
  if (!s_renderdoc_lib)
    {
      if (debug)
        fprintf(stderr, "RenderDoc is not running, capture is impossible!\n");
      return;
    }

  pRENDERDOC_GetAPI RENDERDOC_GetAPI =
    (pRENDERDOC_GetAPI)dlsym(s_renderdoc_lib, "RENDERDOC_GetAPI");

  int ret = RENDERDOC_GetAPI(eRENDERDOC_API_Version_1_1_2, (void **)&s_renderdoc_api);
  ASSERT_MSG(ret == 1, "Could not retrieve RenderDoc API info!\n");

  if (!s_renderdoc_api)
    {
      if (debug)
        fprintf(stderr, "RenderDoc API not available, capture is impossible!\n");

      return;
    }

  if (debug)
    printf("ENABLING RENDERDOC CAPTURE\n");

  s_renderdoc_api->StartFrameCapture(NULL, NULL);
}

static void
renderdoc_capture_teardown(void)
{
  if (s_renderdoc_api != NULL)
    {
      s_renderdoc_api->EndFrameCapture(NULL, NULL);
    }
  if (s_renderdoc_lib != NULL)
    dlclose(s_renderdoc_lib);
}

#endif  // USE_RENDERDOC_CAPTURE

//
//
//

typedef struct
{
  uint32_t                  count;
  VkQueueFamilyProperties * props;
} QueueFamilies;

static void
queue_families_init(QueueFamilies * families, VkPhysicalDevice physical_device)
{
  vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &families->count, NULL);
  families->props = calloc(families->count, sizeof(families->props[0]));
  vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &families->count, families->props);
}

static void
queue_families_destroy(QueueFamilies * families)
{
  if (families->count)
    {
      free(families->props);
      families->props = NULL;
      families->count = 0;
    }
}

static bool
queue_families_find_for_flags(const QueueFamilies * families,
                              uint32_t              wanted_flags,
                              uint32_t *            pFamily)
{
  for (uint32_t nn = 0; nn < families->count; ++nn)
    {
      if (families->props[nn].queueCount > 0 &&
          (families->props[nn].queueFlags & wanted_flags) == wanted_flags)
        {
          *pFamily = nn;
          return true;
        }
    }
  return false;
}

//
//  Debug report support
//

// TODO(digit): Change this to use debug utils instead.

static VkBool32 VKAPI_PTR
debug_report_callback(VkDebugReportFlagsEXT      flags,
                      VkDebugReportObjectTypeEXT objectType,
                      uint64_t                   object,
                      size_t                     location,
                      int32_t                    messageCode,
                      char const *               pLayerPrefix,
                      char const *               pMessage,
                      void *                     pUserData)
{
#define LIST_LEVELS(macro) macro(WARNING) macro(PERFORMANCE_WARNING) macro(ERROR) macro(DEBUG)

  const char * flag = NULL;

  switch (flags)
    {
// For each level, set |flag| appropriately.
#define CASE_FOR_LEVEL(name)                                                                       \
  case VK_DEBUG_REPORT_##name##_BIT_EXT:                                                           \
    flag = "VK_DEBUG_REPORT_" #name "_BIT_EXT";                                                    \
    break;

      LIST_LEVELS(CASE_FOR_LEVEL)

#undef CASE_FOR_LEVEL

      default:;
    }
  if (flag)
    {
      fprintf(stderr, "%s %s %s\n", flag, pLayerPrefix, pMessage);
    }
  return VK_FALSE;
}

static bool
setup_debug_report(VkInstance instance, VkDebugReportCallbackEXT * pCallback)
{
  GET_VULKAN_INSTANCE_PROC_ADDR(vkCreateDebugReportCallbackEXT);
  if (!vkCreateDebugReportCallbackEXT)
    return false;

  vk(CreateDebugReportCallbackEXT(
    instance,
    &(const VkDebugReportCallbackCreateInfoEXT){
      .sType = VK_STRUCTURE_TYPE_DEBUG_REPORT_CALLBACK_CREATE_INFO_EXT,
      .flags = VK_DEBUG_REPORT_INFORMATION_BIT_EXT | VK_DEBUG_REPORT_WARNING_BIT_EXT |
               VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT | VK_DEBUG_REPORT_ERROR_BIT_EXT |
               VK_DEBUG_REPORT_DEBUG_BIT_EXT,
      .pfnCallback = debug_report_callback,
      .pUserData   = NULL,
    },
    NULL,
    pCallback));

  return true;
}

//
//  Fuchsia specific application state.
//

#if VK_USE_PLATFORM_FUCHSIA
typedef struct
{
  async_loop_t *     async_loop;
  trace_provider_t * trace_provider;
} FuchsiaState;

static void
fuchsia_state_init(FuchsiaState * state, bool need_tracing)
{
  zx_status_t status =
    async_loop_create(&kAsyncLoopConfigNoAttachToCurrentThread, &state->async_loop);
  ASSERT_MSG(status == ZX_OK, "async_loop_create failed.\n");

  status = async_loop_start_thread(state->async_loop, "loop", NULL);
  ASSERT_MSG(status == ZX_OK, "async_loop_start_thread failed.\n");

  // NOTE: Creating the trace provider can fail randomly on the CQ, so only
  // try to do it when needed (see https://crbug.com/fuchsia/41918).
  if (need_tracing)
    {
      async_dispatcher_t * dispatcher = async_loop_get_dispatcher(state->async_loop);
      state->trace_provider           = trace_provider_create_with_fdio(dispatcher);
      ASSERT_MSG(state->trace_provider != NULL, "trace_provider_create failed.\n");
    }
}

static void
fuchsia_state_destroy(FuchsiaState * state)
{
  if (state->trace_provider)
    trace_provider_destroy(state->trace_provider);

  async_loop_shutdown(state->async_loop);
}
#endif  // VK_USE_PLATFORM_FUCHSIA

//
//
//

typedef struct
{
  VkDebugReportCallbackEXT drc;
#if VK_USE_PLATFORM_FUCHSIA
  FuchsiaState fuchsia;
#endif

} AppStateInternal;

bool
vk_app_state_init(vk_app_state_t * app_state, const vk_app_state_config_t * config)
{
#if USE_RENDERDOC_CAPTURE
  renderdoc_capture_setup(config->enable_debug_report);
#endif

  *app_state = (const vk_app_state_t){};

  InstanceInfo instance_info = instance_info_create();
  if (config->enable_debug_report)
    {
      // For debugging only!
      instance_info_print(&instance_info);
    }

  //
  // create a Vulkan instances
  //
  VkApplicationInfo const app_info = {

    .sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO,
    .pNext              = NULL,
    .pApplicationName   = config->app_name ? config->app_name : "VK Test",
    .applicationVersion = 0,
    .pEngineName        = config->engine_name ? config->engine_name : "Graphics Compute VK",
    .engineVersion      = 0,
    .apiVersion         = VK_API_VERSION_1_1
  };

  StringList enabled_layers     = STRING_LIST_INITIALIZER;
  StringList enabled_extensions = STRING_LIST_INITIALIZER;

  bool enable_validation = config->enable_validation || config->enable_debug_report;

  if (enable_validation)
    {
      // VK_LAYER_KHRONOS_validation is the new hotness to use, but not
      // all Vulkan installs support it yet, so fallback to
      // VK_LAYER_LUNARG_standard_validation if it is not available.
      static const char * const validation_layer_names[] = {
        "VK_LAYER_KHRONOS_validation",
        "VK_LAYER_LUNARG_standard_validation",
      };
      for (uint32_t nn = 0; nn < ARRAY_SIZE(validation_layer_names); ++nn)
        {
          const char * layer_name = validation_layer_names[nn];
          if (instance_info_has_layer(&instance_info, layer_name))
            {
              string_list_append(&enabled_layers, layer_name);
              break;
            }
        }
    }

  if (config->enable_debug_report)
    {
      if (instance_info_has_extension(&instance_info, VK_EXT_DEBUG_REPORT_EXTENSION_NAME))
        {
          string_list_append(&enabled_extensions, VK_EXT_DEBUG_REPORT_EXTENSION_NAME);
        }
      app_state->has_debug_report = true;
    }

  bool require_swapchain = config->require_swapchain;

  if (require_swapchain)
    {
      string_list_append(&enabled_extensions, VK_KHR_SURFACE_EXTENSION_NAME);

      // NOTE: On Fuchsia, swapchain extensions are provided by a layer.
      // For now, only use the layer allowing presenting to the framebuffer
      // directly (another layer is provided to display in a window, but this one
      // is far more work to get everything working).
#if VK_USE_PLATFORM_FUCHSIA
      const char * fuchsia_layer = config->disable_swapchain_present
                                     ? "VK_LAYER_FUCHSIA_imagepipe_swapchain_fb_skip_present"
                                     : "VK_LAYER_FUCHSIA_imagepipe_swapchain_fb";
      string_list_append(&enabled_layers, fuchsia_layer);
      string_list_append(&enabled_extensions, VK_FUCHSIA_IMAGEPIPE_SURFACE_EXTENSION_NAME);
#else
      glfwInit();
      uint32_t      glfw_extensions_count = 0;
      const char ** glfw_extensions = glfwGetRequiredInstanceExtensions(&glfw_extensions_count);
      for (uint32_t n = 0; n < glfw_extensions_count; ++n)
        {
          string_list_append(&enabled_extensions, glfw_extensions[n]);
        }
      if (config->disable_swapchain_present)
        fprintf(stderr, "WARNING: disable_swapchain_present ignored on this platform!\n");
#endif
    }
  else if (config->disable_swapchain_present)
    {
      fprintf(stderr,
              "WARNING: disable_swapchain_present ignored, since require_swapchain isn't set!\n");
    }

  VkInstanceCreateInfo const instance_create_info = {

    .sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
    .pNext                   = NULL,
    .flags                   = 0,
    .pApplicationInfo        = &app_info,
    .enabledLayerCount       = enabled_layers.count,
    .ppEnabledLayerNames     = enabled_layers.items,
    .enabledExtensionCount   = enabled_extensions.count,
    .ppEnabledExtensionNames = enabled_extensions.items,
  };

  if (!instance_info_validate_layers_and_extensions(&instance_info,
                                                    &enabled_layers,
                                                    &enabled_extensions))
    {
      // NOTE: Error message printed by called function above.
      instance_info_destroy(&instance_info);
      return false;
    }

  vk(CreateInstance(&instance_create_info, NULL, &app_state->instance));

  if (config->enable_debug_report)
    vk_instance_create_info_print(&instance_create_info);

  instance_info_destroy(&instance_info);

  //
  //
  //

  AppStateInternal * internal = calloc(1, sizeof(*internal));
  app_state->internal         = internal;

#if VK_USE_PLATFORM_FUCHSIA
  fuchsia_state_init(&internal->fuchsia, config->enable_tracing);
#endif

  if (app_state->has_debug_report)
    {
      if (!setup_debug_report(app_state->instance, &internal->drc))
        {
          fprintf(stderr, "WARNING: vkCreateDebugReportCallbackEXT not supported by Vulkan ICD!");
          app_state->has_debug_report = false;
        }
    }

  //
  // Prepare Vulkan environment for Spinel
  //
  app_state->d           = VK_NULL_HANDLE;
  app_state->ac          = NULL;
  app_state->pc          = VK_NULL_HANDLE;
  app_state->pd          = VK_NULL_HANDLE;
  app_state->qfi         = UINT32_MAX;
  app_state->compute_qfi = UINT32_MAX;

  //
  // acquire all physical devices
  //
  GpuList gpus;
  gpu_list_init(&gpus, app_state->instance);

  if (gpus.count == 0)
    {
      fprintf(stderr, "No Vulkan device available!\n");
      return false;
    }

  // Grab device configuration through callback is necessary.
  vk_device_config_t device_config = config->device_config;
  if (config->device_config_callback != NULL)
    {
      bool found = false;
      for (uint32_t nn = 0; nn < gpus.count; ++nn)
        {
          device_config = (const vk_device_config_t){};

          if (config->device_config_callback(config->device_config_opaque,
                                             app_state->instance,
                                             gpus.devices[nn],
                                             &device_config))
            {
              device_config.physical_device = gpus.devices[nn];
              found                         = true;
              break;
            }
        }
    }

  // Find the appropriate physical device
  if (device_config.physical_device == VK_NULL_HANDLE)
    {
      //
      // If vendor_id if provided, look for corresponding matches.
      if (device_config.vendor_id != 0)
        {
          for (uint32_t nn = 0; nn < gpus.count; ++nn)
            {
              VkPhysicalDeviceProperties props;
              vkGetPhysicalDeviceProperties(gpus.devices[nn], &props);

              if (props.vendorID != device_config.vendor_id)
                continue;

              if (device_config.device_id != 0 && props.deviceID != device_config.device_id)
                continue;

              // Found a match!
              device_config.physical_device = gpus.devices[nn];
              break;
            }

          if (!device_config.physical_device)
            {
              fprintf(stderr,
                      "Device with (vendorID=%X,deviceID=%X) not found.\n",
                      device_config.vendor_id,
                      device_config.device_id);
              gpu_list_destroy(&gpus);
              return false;
            }
        }
      else
        {
          // Use the first one by default.
          device_config.physical_device = gpus.devices[0];
        }
      gpu_list_destroy(&gpus);
    }

  //
  // get the physical device's memory props
  //
  ASSERT_MSG(device_config.physical_device != VK_NULL_HANDLE, "Missing physical device.\n");

  app_state->pd = device_config.physical_device;
  vkGetPhysicalDeviceProperties(app_state->pd, &app_state->pdp);
  vkGetPhysicalDeviceMemoryProperties(app_state->pd, &app_state->pdmp);

  //
  // get image properties
  //
  // vkGetPhysicalDeviceImageFormatProperties()
  //
  // vk(GetPhysicalDeviceImageFormatProperties(phy_device,
  //

  //
  // Find appropriate queue families.
  //

  uint32_t graphics_family = UINT32_MAX;
  uint32_t compute_family  = UINT32_MAX;

  const VkQueueFlags wanted_combined_queues = device_config.required_combined_queues;
  VkQueueFlags       wanted_queues = wanted_combined_queues | device_config.required_queues;

  // Enabling the swapchain requires the graphics queue.
  // Otherwise, use the graphics queue by default if none was asked.
  if (require_swapchain || !wanted_queues)
    wanted_queues |= VK_QUEUE_GRAPHICS_BIT;

  {
    QueueFamilies families;
    queue_families_init(&families, app_state->pd);

    // First, try to find combined queues if requested.
    if (wanted_combined_queues)
      {
        uint32_t family;
        if (!queue_families_find_for_flags(&families, wanted_combined_queues, &family))
          {
            queue_families_destroy(&families);
            fprintf(stderr, "This device does not supported the required combined queues!\n");
            return false;
          }

        if ((wanted_combined_queues & VK_QUEUE_GRAPHICS_BIT) != 0)
          graphics_family = family;

        if ((wanted_combined_queues & VK_QUEUE_COMPUTE_BIT) != 0)
          compute_family = family;
      }

    // Then find other queues if requested.
    VkQueueFlags single_queues = wanted_queues & ~wanted_combined_queues;
    if (single_queues)
      {
        uint32_t family;
        // First, try to find combined queues if any.
        if (queue_families_find_for_flags(&families, single_queues, &family))
          {
            if ((single_queues & VK_QUEUE_GRAPHICS_BIT) != 0 && graphics_family == UINT32_MAX)
              {
                graphics_family = family;
              }
            if ((single_queues & VK_QUEUE_COMPUTE_BIT) != 0 && compute_family == UINT32_MAX)
              {
                compute_family = family;
              }
          }
        else
          {
            // Otherwise, try each bit in isolation.
            if ((single_queues & VK_QUEUE_GRAPHICS_BIT) != 0 && graphics_family == UINT32_MAX)
              {
                (void)queue_families_find_for_flags(&families,
                                                    VK_QUEUE_GRAPHICS_BIT,
                                                    &graphics_family);
              }
            if ((single_queues & VK_QUEUE_COMPUTE_BIT) != 0 && compute_family == UINT32_MAX)
              {
                (void)queue_families_find_for_flags(&families,
                                                    VK_QUEUE_COMPUTE_BIT,
                                                    &compute_family);
              }
          }
      }

    queue_families_destroy(&families);
  }

  // Sanity checks.
  if ((wanted_queues & VK_QUEUE_GRAPHICS_BIT) != 0 && graphics_family == UINT32_MAX)
    {
      fprintf(stderr, "This device does not provide a graphics queue!\n");
      return false;
    }

  if ((wanted_queues & VK_QUEUE_COMPUTE_BIT) != 0 && compute_family == UINT32_MAX)
    {
      fprintf(stderr, "This device does not provide a compute queue!\n");
      return false;
    }

  if (require_swapchain)
    {
      if (!physical_device_supports_presentation(app_state->pd,
                                                 graphics_family,
                                                 app_state->instance))
        {
          fprintf(stderr, "This device does not support presentation/display!\n");
          return false;
        }
    }

  app_state->qfi         = graphics_family;
  app_state->compute_qfi = compute_family;

  //
  // create queues
  //
  uint32_t queue_families[2]  = {};
  uint32_t queue_family_count = 0;

  if (graphics_family != UINT32_MAX)
    {
      queue_families[queue_family_count++] = graphics_family;
    }
  if (compute_family != UINT32_MAX && compute_family != graphics_family)
    {
      queue_families[queue_family_count++] = compute_family;
    }

  VkDeviceQueueCreateInfo queue_family_info[2];
  for (uint32_t nn = 0; nn < queue_family_count; ++nn)
    {
      queue_family_info[nn] = (const VkDeviceQueueCreateInfo){
        .sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .pNext            = NULL,
        .flags            = 0,
        .queueFamilyIndex = queue_families[nn],
        .queueCount       = 1,
        .pQueuePriorities = &(const float){ 1.0f },
      };
    }

  StringList device_extensions = STRING_LIST_INITIALIZER;

  //
  // Enable optional device extensions if they are available first.
  //
  {
    static const char subgroup_size_control_ext[] = "VK_EXT_subgroup_size_control";
    static const char amd_shader_info_ext[]       = VK_AMD_SHADER_INFO_EXTENSION_NAME;

    VkInstance instance    = app_state->instance;
    DeviceInfo device_info = device_info_create(instance, app_state->pd);

    // VK_EXT_subgroup_size_control
    if (config->enable_subgroup_size_control &&
        device_info_has_extension(&device_info, subgroup_size_control_ext))
      {
        string_list_append(&device_extensions, subgroup_size_control_ext);
        app_state->has_subgroup_size_control = true;
      }

    // VK_AMD_shader_info
    if (config->enable_amd_statistics &&
        device_info_has_extension(&device_info, amd_shader_info_ext))
      {
        string_list_append(&device_extensions, amd_shader_info_ext);
        app_state->has_amd_statistics = true;
      }

    device_info_destroy(&device_info);
  }

  //
  // Enable swapchain device extension if needed.
  //
  if (require_swapchain)
    {
      string_list_append(&device_extensions, VK_KHR_SWAPCHAIN_EXTENSION_NAME);
    }

  //
  // Merge required device extensions now.
  string_list_add_n(&device_extensions,
                    device_config.extensions_count,
                    device_config.extensions_names);

  VkDeviceCreateInfo const device_info = {

    .sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
    .pNext                   = NULL,
    .flags                   = 0,
    .queueCreateInfoCount    = queue_family_count,
    .pQueueCreateInfos       = queue_family_info,
    .enabledLayerCount       = 0,
    .ppEnabledLayerNames     = NULL,
    .enabledExtensionCount   = device_extensions.count,
    .ppEnabledExtensionNames = device_extensions.items,
    .pEnabledFeatures        = &device_config.features,
  };

  vk(CreateDevice(app_state->pd, &device_info, app_state->ac, &app_state->d));

  if (config->enable_debug_report)
    vk_device_create_info_print(&device_info);

  //
  // create the pipeline cache
  //
  if (config->enable_pipeline_cache)
    {
      app_state->pc = pipeline_cache_load(PIPELINE_CACHE_FILE_PATH, app_state->d, app_state->ac);
    }

  return true;
}

void
vk_app_state_destroy(vk_app_state_t * app_state)
{
  if (app_state->pc != VK_NULL_HANDLE)
    {
      (void)
        pipeline_cache_save(app_state->pc, PIPELINE_CACHE_FILE_PATH, app_state->d, app_state->ac);
    }

  vkDestroyDevice(app_state->d, app_state->ac);

  AppStateInternal * internal = app_state->internal;

  if (app_state->has_debug_report && internal)
    {
      VkInstance instance = app_state->instance;
      GET_VULKAN_INSTANCE_PROC_ADDR(vkDestroyDebugReportCallbackEXT);
      vkDestroyDebugReportCallbackEXT(app_state->instance, internal->drc, NULL);
    }

#if VK_USE_PLATFORM_FUCHSIA
  fuchsia_state_destroy(&internal->fuchsia);
#endif

  free(internal);

  vkDestroyInstance(app_state->instance, NULL);

#if USE_RENDERDOC_CAPTURE
  renderdoc_capture_teardown();
#endif
}

vk_queue_families_t
vk_app_state_get_queue_families(const vk_app_state_t * app_state)
{
  vk_queue_families_t result;
  uint32_t            count = 0;
  if (app_state->qfi != UINT32_MAX)
    result.indices[count++] = app_state->qfi;

  if (app_state->compute_qfi != UINT32_MAX)
  {
	ASSERT(count == 0 || count == 1);
	if (count == 0 || app_state->compute_qfi != result.indices[0])
		result.indices[count++] = app_state->compute_qfi;
  }

  result.count = count;
  return result;
}

bool
vk_app_state_poll_events(vk_app_state_t * app_state)
{
#if VK_USE_PLATFORM_FUCHSIA
  // TODO(digit): Find a way to receive events from the system.
  return true;
#else
  return glfw_poll_events();
#endif
}

void
vk_app_state_print(const vk_app_state_t * app_state)
{
  const uint32_t vendor_id = app_state->pdp.vendorID;
  const uint32_t device_id = app_state->pdp.deviceID;

  printf("Device (vendor_id, device_id)=(0x%X, 0x%0X)\n", vendor_id, device_id);
  printf("  VkInstance:            %p\n", app_state->instance);
  printf("  Allocation callbacks:  %p\n", app_state->ac);
  printf("  VkPhysicalDevice:      %p\n", app_state->pd);
  printf("  VkDevice:              %p\n", app_state->d);

  printf("  VkPhysicalDeviceProperties:\n");
  printf("     apiVersion:       0x%x\n", app_state->pdp.apiVersion);
  printf("     driverVersion:    0x%x\n", app_state->pdp.driverVersion);
  printf("     vendorID:         0x%x\n", app_state->pdp.vendorID);
  printf("     deviceID:         0x%x\n", app_state->pdp.deviceID);
  printf("     deviceType:       %s\n",
         vk_physical_device_type_to_string(app_state->pdp.deviceType));
  printf("     deviceName:       %s\n", app_state->pdp.deviceName);

  printf("  VkPhysicalDeviceMemoryProperties:\n");
  for (uint32_t n = 0; n < app_state->pdmp.memoryHeapCount; ++n)
    {
      printf("      heap index=%-2d %s\n",
             n,
             vk_memory_heap_to_string(&app_state->pdmp.memoryHeaps[n]));
    }
  for (uint32_t n = 0; n < app_state->pdmp.memoryTypeCount; ++n)
    {
      printf("      type index=%-2d %s\n",
             n,
             vk_memory_type_to_string(&app_state->pdmp.memoryTypes[n]));
    }

  printf("  has_debug_report:           %s\n", app_state->has_debug_report ? "true" : "false");
  printf("  has_amd_statistics:         %s\n", app_state->has_amd_statistics ? "true" : "false");
  printf("  has_debug_report:           %s\n", app_state->has_debug_report ? "true" : "false");
  printf("  has_subgroup_size_control:  %s\n",
         app_state->has_subgroup_size_control ? "true" : "false");

  printf("  Queue families:\n");
  printf("    Graphics:  %s\n", vk_queue_family_index_to_string(app_state->qfi));
  printf("    Compute:   %s\n", vk_queue_family_index_to_string(app_state->compute_qfi));
}

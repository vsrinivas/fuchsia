// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vk_strings.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "tests/common/utils.h"

//
//
//

// TODO(digit): Decide what to do to make ring buffer allocation thread-safe
// if this becomes an issue. There are essentially two ways to do it:
//
//  A) Make |s_ring| below thread_local. Not supported by C99 by default but
//     easy to add with compiler-specific extensions for GCC, Clang and MSVC.
//     A bit wasteful though.
//
//  B) Use atomic loads and compare-and-swaps to use a single global buffer
//     with an atomically-managed |pos| pointer below. A little more difficult
//     to implement due to the lack of <stdatomic.h> in C99. Again,
//     compiler-specific extensions can be used.
//

// A global ring buffer for temporary string buffers used by these functions.
#define RING_SIZE (2048u - sizeof(size_t))

typedef struct
{
  size_t pos;
  char   data[RING_SIZE];
} Ring;

static char *
ring_alloc(Ring * ring, size_t size)
{
  ASSERT_MSG(size < RING_SIZE,
             "String ring allocation too large %u > %u\n",
             (uint32_t)size,
             RING_SIZE);
  size_t avail  = RING_SIZE - ring->pos;
  char * result = ring->data + ring->pos;
  if (size <= avail)
    {
      ring->pos += size;
    }
  else
    {
      result    = ring->data;
      ring->pos = 0;
    }
  return result;
}

// TODO(digit): Make this thread_local eventually to make all functions here
// thread-safe. For now, just use a single static ring.
static Ring s_ring;

// Allocate |size_| bytes of data from the current thread's string ring.
#define DECLARE_TEMP_ARRAY(temp_, size_) char * temp_ = ring_alloc(&s_ring, size_)

//
//
//

// Helper type for a fixed-size buffer that can accept formatted data.
typedef struct
{
  char * data;
  size_t capacity;
  size_t pos;
} Buffer;

void
buffer_init(Buffer * buffer, char * data, size_t data_size)
{
  ASSERT_MSG(data_size > 0, "Cannot create buffer with size of 0\n");
  buffer->data     = data;
  buffer->pos      = 0;
  buffer->capacity = data_size - 1;
  buffer->data[0]  = 0;
  ;
}

void
buffer_add(Buffer * buffer, const char * str, size_t size)
{
  size_t avail = buffer->capacity;
  if (size > avail)
    size = avail;
  memmove(buffer->data + buffer->pos, str, size);
  buffer->pos += size;
  buffer->data[buffer->pos] = 0;
}

void
buffer_add_s(Buffer * buffer, const char * str)
{
  buffer_add(buffer, str, strlen(str));
}

void
buffer_add_formatv(Buffer * buffer, const char * fmt, va_list args)
{
  size_t avail = buffer->capacity;
  if (!avail)
    return;

  int len = vsnprintf(buffer->data + buffer->pos, avail, fmt, args);
  ASSERT_MSG(len >= 0, "vsnprintf() error!\n");
  if ((size_t)len >= avail)
    buffer->pos = buffer->capacity;
  else
    buffer->pos += len;
  buffer->data[buffer->pos] = 0;
}

void
buffer_add_format(Buffer * buffer, const char * fmt, ...)
{
  va_list args;
  va_start(args, fmt);
  buffer_add_formatv(buffer, fmt, args);
  va_end(args);
}

// Declare a static temporary buffer. This could become thread_local if we
// really need to make these functions thread-safe.
#define DECLARE_TEMP_BUFFER(temp_, size_)                                                          \
  DECLARE_TEMP_ARRAY(MACRO_EXPAND(temp_##_data), size_);                                           \
  Buffer temp_[1];                                                                                 \
  buffer_init(temp_, temp_##_data, size_)

extern const char *
vk_device_size_to_string(VkDeviceSize size)
{
  DECLARE_TEMP_ARRAY(temp, 16);
  if (size < 65536)
    {
      snprintf(temp, sizeof(temp), "%u", (unsigned)size);
    }
  else if (size < 1024 * 1024)
    {
      snprintf(temp, sizeof(temp), "%.1f kiB", size / 1024.);
    }
  else if (size < 1024 * 1024 * 1024)
    {
      snprintf(temp, sizeof(temp), "%.1f MiB", size / (1024. * 1024.));
    }
  else
    {
      snprintf(temp, sizeof(temp), "%1.f GiB", size / (1024. * 1024. * 1024.));
    }
  return temp;
}

const char *
vk_queue_family_index_to_string(uint32_t queue_family_index)
{
  if (queue_family_index == UINT32_MAX)
    return "NONE";

  DECLARE_TEMP_ARRAY(temp, 10);
  snprintf(temp, sizeof(temp), "%u", queue_family_index);
  return temp;
}

const char *
vk_memory_heap_to_string(const VkMemoryHeap * memory_heap)
{
  DECLARE_TEMP_BUFFER(temp, 64);
  uint32_t flags = memory_heap->flags;
  buffer_add_format(temp,
                    "size=%-8s flags=0x%08X",
                    vk_device_size_to_string(memory_heap->size),
                    flags);
#define FLAG_BIT(name_)                                                                            \
  if ((flags & VK_MEMORY_HEAP_##name_##_BIT) != 0)                                                 \
    buffer_add_format(temp, " %s", #name_);

  FLAG_BIT(DEVICE_LOCAL);
  FLAG_BIT(MULTI_INSTANCE);
#undef FLAG_BIT
  return temp->data;
}

const char *
vk_memory_type_to_string(const VkMemoryType * memory_type)
{
  DECLARE_TEMP_BUFFER(temp, 64);
  uint32_t flags = memory_type->propertyFlags;
  buffer_add_format(temp, "heap=%-2d flags=0x%08X", memory_type->heapIndex, flags);

#define FLAG_BIT(name_)                                                                            \
  if ((flags & VK_MEMORY_PROPERTY_##name_##_BIT))                                                  \
    buffer_add_format(temp, " %s", #name_);

  FLAG_BIT(DEVICE_LOCAL);
  FLAG_BIT(HOST_VISIBLE);
  FLAG_BIT(HOST_COHERENT);
  FLAG_BIT(HOST_CACHED);
  FLAG_BIT(LAZILY_ALLOCATED);
  FLAG_BIT(PROTECTED);

#undef FLAG_BIT
  return temp->data;
}

const char *
vk_present_mode_khr_to_string(VkPresentModeKHR arg)
{
#define CASE(arg_)                                                                                 \
  case VK_PRESENT_MODE_##arg_:                                                                     \
    return "VK_PRESENT_MODE_" #arg_
  switch (arg)
    {
      CASE(IMMEDIATE_KHR);
      CASE(MAILBOX_KHR);
      CASE(FIFO_KHR);
      CASE(FIFO_RELAXED_KHR);
      default:;
    }
#undef CASE
  DECLARE_TEMP_ARRAY(temp, 16);
  snprintf(temp, sizeof(temp), "UNKNOWN(%u)", (unsigned)arg);
  return temp;
}

const char *
vk_format_to_string(VkFormat arg)
{
#define CASE(arg_)                                                                                 \
  case VK_FORMAT_##arg_:                                                                           \
    return "VK_FORMAT_" #arg_
  switch (arg)
    {
      CASE(UNDEFINED);
      CASE(B8G8R8A8_UNORM);
      CASE(B8G8R8A8_SRGB);
      CASE(R8G8B8A8_UNORM);
      CASE(R8G8B8A8_SRGB);
      default:;
    }
#undef CASE
  DECLARE_TEMP_ARRAY(temp, 16);
  snprintf(temp, sizeof(temp), "UNKNOWN(%u)", (unsigned)arg);
  return temp;
}

const char *
vk_colorspace_khr_to_string(VkColorSpaceKHR arg)
{
#define CASE(arg_)                                                                                 \
  case VK_COLOR_SPACE_##arg_:                                                                      \
    return "VK_COLOR_SPACE_" #arg_
  switch (arg)
    {
      CASE(SRGB_NONLINEAR_KHR);
      default:;
    }
#undef CASE
  DECLARE_TEMP_ARRAY(temp, 16);
  snprintf(temp, sizeof(temp), "UNKNOWN(%u)", (unsigned)arg);
  return temp;
}

const char *
vk_surface_format_khr_to_string(VkSurfaceFormatKHR format)
{
  DECLARE_TEMP_ARRAY(temp, 32);
  snprintf(temp,
           sizeof(temp),
           "%s(%s)",
           vk_format_to_string(format.format),
           vk_colorspace_khr_to_string(format.colorSpace));
  return temp;
}

const char *
vk_format_feature_flags_to_string(VkFormatFeatureFlags flags)
{
  DECLARE_TEMP_BUFFER(temp, 128);

#define CHECK_FLAG(flag_)                                                                          \
  if ((flags & VK_FORMAT_FEATURE_##flag_##_BIT) != 0)                                              \
    buffer_add_format(temp, " %s", #flag_);

  CHECK_FLAG(SAMPLED_IMAGE)
  CHECK_FLAG(STORAGE_IMAGE)
  CHECK_FLAG(STORAGE_IMAGE_ATOMIC)
  CHECK_FLAG(UNIFORM_TEXEL_BUFFER)
  CHECK_FLAG(STORAGE_TEXEL_BUFFER)
  CHECK_FLAG(STORAGE_TEXEL_BUFFER_ATOMIC)
  CHECK_FLAG(VERTEX_BUFFER)
  CHECK_FLAG(COLOR_ATTACHMENT)
  CHECK_FLAG(COLOR_ATTACHMENT_BLEND)
  CHECK_FLAG(DEPTH_STENCIL_ATTACHMENT)
  CHECK_FLAG(BLIT_SRC)
  CHECK_FLAG(BLIT_DST)

#undef CHECK_FLAG

  return temp->data;
}

#define LIST_VK_IMAGE_USAGE_BITS(macro)                                                            \
  macro(TRANSFER_SRC) macro(TRANSFER_DST) macro(SAMPLED) macro(STORAGE) macro(COLOR_ATTACHMENT)    \
    macro(DEPTH_STENCIL_ATTACHMENT) macro(TRANSIENT_ATTACHMENT) macro(INPUT_ATTACHMENT)

const char *
vk_image_usage_flags_to_string(VkImageUsageFlags flags)
{
  uint32_t bits = (uint32_t)flags;

  if (!bits)
    return "NONE";

#define TEST_BIT(bit_) ((bits & VK_IMAGE_USAGE_##bit_##_BIT) != 0)

    // First, count the number of known bits set.
#define COUNT_BIT(bit_)                                                                            \
  if (TEST_BIT(bit_))                                                                              \
    count++;
  int count = 0;
  LIST_VK_IMAGE_USAGE_BITS(COUNT_BIT)
#undef COUNT_BIT

  DECLARE_TEMP_BUFFER(temp, 64);
  if (!count)
    {
      buffer_add_format(temp, "UNKNOWN(0x%X)", bits);
      return temp->data;
    }

#define NAME_BIT(bit_) #bit_,
  static const char * const kBitNames[] = { LIST_VK_IMAGE_USAGE_BITS(NAME_BIT) };
#undef NAME_BIT

  buffer_add_s(temp, "VK_IMAGE_USAGE_");
  if (count > 1)
    buffer_add(temp, "[", 1);

  const char * separator = "";
  unsigned     bit_index = 0;

#define ADD_BIT(bit_)                                                                              \
  if (TEST_BIT(bit_))                                                                              \
    {                                                                                              \
      buffer_add_s(temp, separator);                                                               \
      buffer_add_s(temp, kBitNames[bit_index]);                                                    \
      separator = "|";                                                                             \
    }                                                                                              \
  bit_index++;

  LIST_VK_IMAGE_USAGE_BITS(ADD_BIT)

#undef ADD_BIT

  if (count > 1)
    buffer_add(temp, "]", 1);

  buffer_add(temp, "_BIT", 4);
  return temp->data;

#undef TEST_BIT
}

#define LIST_VK_BUFFER_USAGE_BITS(macro)                                                           \
  macro(TRANSFER_SRC) macro(TRANSFER_DST) macro(UNIFORM_TEXEL_BUFFER) macro(STORAGE_TEXEL_BUFFER)  \
    macro(UNIFORM_BUFFER) macro(STORAGE_BUFFER) macro(INDEX_BUFFER) macro(VERTEX_BUFFER)           \
      macro(INDIRECT_BUFFER)

const char *
vk_buffer_usage_flags_to_string(VkBufferUsageFlags flags)
{
  uint32_t bits = (uint32_t)flags;

  if (!bits)
    return "NONE";

#define TEST_BIT(bit_) ((bits & VK_BUFFER_USAGE_##bit_##_BIT) != 0)

    // First, count the number of known bits set.
#define COUNT_BIT(bit_)                                                                            \
  if (TEST_BIT(bit_))                                                                              \
    count++;
  int count = 0;
  LIST_VK_BUFFER_USAGE_BITS(COUNT_BIT)
#undef COUNT_BIT

  // Special case if there are no known bits.
  DECLARE_TEMP_BUFFER(temp, 64);
  if (!count)
    {
      buffer_add_format(temp, "UNKNOWN(0x%X)", bits);
      return temp->data;
    }

#define NAME_BIT(bit_) #bit_,
  static const char * const kBitNames[] = { LIST_VK_BUFFER_USAGE_BITS(NAME_BIT) };
#undef NAME_BIT

  buffer_add_s(temp, "VK_BUFFER_USAGE_");
  if (count > 1)
    buffer_add(temp, "[", 1);

  const char * separator = "";
  unsigned     bit_index = 0;

#define ADD_BIT(bit_)                                                                              \
  if (TEST_BIT(bit_))                                                                              \
    {                                                                                              \
      buffer_add(temp, separator, 1);                                                              \
      buffer_add_s(temp, kBitNames[bit_index]);                                                    \
      separator = "|";                                                                             \
    }                                                                                              \
  bit_index++;

  LIST_VK_BUFFER_USAGE_BITS(ADD_BIT)

#undef ADD_BIT

  if (count > 1)
    buffer_add(temp, "]", 1);

  buffer_add(temp, "_BIT", 4);
  return temp->data;

#undef TEST_BIT
}

const char *
vk_physical_device_type_to_string(VkPhysicalDeviceType device_type)
{
#define CASE(type_) case VK_PHYSICAL_DEVICE_TYPE_ ## type_: return #type_;
  switch (device_type)
    {
      CASE(OTHER)
      CASE(INTEGRATED_GPU)
      CASE(DISCRETE_GPU)
      CASE(VIRTUAL_GPU)
      CASE(CPU)
      default:;
    }
#undef CASE
  DECLARE_TEMP_ARRAY(temp, 16);
  snprintf(temp, sizeof(temp), "UKNOWN(%u)", device_type);
  return temp;
}

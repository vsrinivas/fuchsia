// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "surface_debug.h"

#include <stdio.h>
#include <stdlib.h>

#include "common/macros.h"

//
//
//

struct val_str
{
  uint32_t     val;
  char const * str;
};

//
//
//

#define SURFACE_DEBUG_VK_FORMATS()                                                                 \
  SURFACE_DEBUG_VK_FORMAT(UNDEFINED)                                                               \
  SURFACE_DEBUG_VK_FORMAT(R4G4_UNORM_PACK8)                                                        \
  SURFACE_DEBUG_VK_FORMAT(R4G4B4A4_UNORM_PACK16)                                                   \
  SURFACE_DEBUG_VK_FORMAT(B4G4R4A4_UNORM_PACK16)                                                   \
  SURFACE_DEBUG_VK_FORMAT(R5G6B5_UNORM_PACK16)                                                     \
  SURFACE_DEBUG_VK_FORMAT(B5G6R5_UNORM_PACK16)                                                     \
  SURFACE_DEBUG_VK_FORMAT(R5G5B5A1_UNORM_PACK16)                                                   \
  SURFACE_DEBUG_VK_FORMAT(B5G5R5A1_UNORM_PACK16)                                                   \
  SURFACE_DEBUG_VK_FORMAT(A1R5G5B5_UNORM_PACK16)                                                   \
  SURFACE_DEBUG_VK_FORMAT(R8_UNORM)                                                                \
  SURFACE_DEBUG_VK_FORMAT(R8_SNORM)                                                                \
  SURFACE_DEBUG_VK_FORMAT(R8_USCALED)                                                              \
  SURFACE_DEBUG_VK_FORMAT(R8_SSCALED)                                                              \
  SURFACE_DEBUG_VK_FORMAT(R8_UINT)                                                                 \
  SURFACE_DEBUG_VK_FORMAT(R8_SINT)                                                                 \
  SURFACE_DEBUG_VK_FORMAT(R8_SRGB)                                                                 \
  SURFACE_DEBUG_VK_FORMAT(R8G8_UNORM)                                                              \
  SURFACE_DEBUG_VK_FORMAT(R8G8_SNORM)                                                              \
  SURFACE_DEBUG_VK_FORMAT(R8G8_USCALED)                                                            \
  SURFACE_DEBUG_VK_FORMAT(R8G8_SSCALED)                                                            \
  SURFACE_DEBUG_VK_FORMAT(R8G8_UINT)                                                               \
  SURFACE_DEBUG_VK_FORMAT(R8G8_SINT)                                                               \
  SURFACE_DEBUG_VK_FORMAT(R8G8_SRGB)                                                               \
  SURFACE_DEBUG_VK_FORMAT(R8G8B8_UNORM)                                                            \
  SURFACE_DEBUG_VK_FORMAT(R8G8B8_SNORM)                                                            \
  SURFACE_DEBUG_VK_FORMAT(R8G8B8_USCALED)                                                          \
  SURFACE_DEBUG_VK_FORMAT(R8G8B8_SSCALED)                                                          \
  SURFACE_DEBUG_VK_FORMAT(R8G8B8_UINT)                                                             \
  SURFACE_DEBUG_VK_FORMAT(R8G8B8_SINT)                                                             \
  SURFACE_DEBUG_VK_FORMAT(R8G8B8_SRGB)                                                             \
  SURFACE_DEBUG_VK_FORMAT(B8G8R8_UNORM)                                                            \
  SURFACE_DEBUG_VK_FORMAT(B8G8R8_SNORM)                                                            \
  SURFACE_DEBUG_VK_FORMAT(B8G8R8_USCALED)                                                          \
  SURFACE_DEBUG_VK_FORMAT(B8G8R8_SSCALED)                                                          \
  SURFACE_DEBUG_VK_FORMAT(B8G8R8_UINT)                                                             \
  SURFACE_DEBUG_VK_FORMAT(B8G8R8_SINT)                                                             \
  SURFACE_DEBUG_VK_FORMAT(B8G8R8_SRGB)                                                             \
  SURFACE_DEBUG_VK_FORMAT(R8G8B8A8_UNORM)                                                          \
  SURFACE_DEBUG_VK_FORMAT(R8G8B8A8_SNORM)                                                          \
  SURFACE_DEBUG_VK_FORMAT(R8G8B8A8_USCALED)                                                        \
  SURFACE_DEBUG_VK_FORMAT(R8G8B8A8_SSCALED)                                                        \
  SURFACE_DEBUG_VK_FORMAT(R8G8B8A8_UINT)                                                           \
  SURFACE_DEBUG_VK_FORMAT(R8G8B8A8_SINT)                                                           \
  SURFACE_DEBUG_VK_FORMAT(R8G8B8A8_SRGB)                                                           \
  SURFACE_DEBUG_VK_FORMAT(B8G8R8A8_UNORM)                                                          \
  SURFACE_DEBUG_VK_FORMAT(B8G8R8A8_SNORM)                                                          \
  SURFACE_DEBUG_VK_FORMAT(B8G8R8A8_USCALED)                                                        \
  SURFACE_DEBUG_VK_FORMAT(B8G8R8A8_SSCALED)                                                        \
  SURFACE_DEBUG_VK_FORMAT(B8G8R8A8_UINT)                                                           \
  SURFACE_DEBUG_VK_FORMAT(B8G8R8A8_SINT)                                                           \
  SURFACE_DEBUG_VK_FORMAT(B8G8R8A8_SRGB)                                                           \
  SURFACE_DEBUG_VK_FORMAT(A8B8G8R8_UNORM_PACK32)                                                   \
  SURFACE_DEBUG_VK_FORMAT(A8B8G8R8_SNORM_PACK32)                                                   \
  SURFACE_DEBUG_VK_FORMAT(A8B8G8R8_USCALED_PACK32)                                                 \
  SURFACE_DEBUG_VK_FORMAT(A8B8G8R8_SSCALED_PACK32)                                                 \
  SURFACE_DEBUG_VK_FORMAT(A8B8G8R8_UINT_PACK32)                                                    \
  SURFACE_DEBUG_VK_FORMAT(A8B8G8R8_SINT_PACK32)                                                    \
  SURFACE_DEBUG_VK_FORMAT(A8B8G8R8_SRGB_PACK32)                                                    \
  SURFACE_DEBUG_VK_FORMAT(A2R10G10B10_UNORM_PACK32)                                                \
  SURFACE_DEBUG_VK_FORMAT(A2R10G10B10_SNORM_PACK32)                                                \
  SURFACE_DEBUG_VK_FORMAT(A2R10G10B10_USCALED_PACK32)                                              \
  SURFACE_DEBUG_VK_FORMAT(A2R10G10B10_SSCALED_PACK32)                                              \
  SURFACE_DEBUG_VK_FORMAT(A2R10G10B10_UINT_PACK32)                                                 \
  SURFACE_DEBUG_VK_FORMAT(A2R10G10B10_SINT_PACK32)                                                 \
  SURFACE_DEBUG_VK_FORMAT(A2B10G10R10_UNORM_PACK32)                                                \
  SURFACE_DEBUG_VK_FORMAT(A2B10G10R10_SNORM_PACK32)                                                \
  SURFACE_DEBUG_VK_FORMAT(A2B10G10R10_USCALED_PACK32)                                              \
  SURFACE_DEBUG_VK_FORMAT(A2B10G10R10_SSCALED_PACK32)                                              \
  SURFACE_DEBUG_VK_FORMAT(A2B10G10R10_UINT_PACK32)                                                 \
  SURFACE_DEBUG_VK_FORMAT(A2B10G10R10_SINT_PACK32)                                                 \
  SURFACE_DEBUG_VK_FORMAT(R16_UNORM)                                                               \
  SURFACE_DEBUG_VK_FORMAT(R16_SNORM)                                                               \
  SURFACE_DEBUG_VK_FORMAT(R16_USCALED)                                                             \
  SURFACE_DEBUG_VK_FORMAT(R16_SSCALED)                                                             \
  SURFACE_DEBUG_VK_FORMAT(R16_UINT)                                                                \
  SURFACE_DEBUG_VK_FORMAT(R16_SINT)                                                                \
  SURFACE_DEBUG_VK_FORMAT(R16_SFLOAT)                                                              \
  SURFACE_DEBUG_VK_FORMAT(R16G16_UNORM)                                                            \
  SURFACE_DEBUG_VK_FORMAT(R16G16_SNORM)                                                            \
  SURFACE_DEBUG_VK_FORMAT(R16G16_USCALED)                                                          \
  SURFACE_DEBUG_VK_FORMAT(R16G16_SSCALED)                                                          \
  SURFACE_DEBUG_VK_FORMAT(R16G16_UINT)                                                             \
  SURFACE_DEBUG_VK_FORMAT(R16G16_SINT)                                                             \
  SURFACE_DEBUG_VK_FORMAT(R16G16_SFLOAT)                                                           \
  SURFACE_DEBUG_VK_FORMAT(R16G16B16_UNORM)                                                         \
  SURFACE_DEBUG_VK_FORMAT(R16G16B16_SNORM)                                                         \
  SURFACE_DEBUG_VK_FORMAT(R16G16B16_USCALED)                                                       \
  SURFACE_DEBUG_VK_FORMAT(R16G16B16_SSCALED)                                                       \
  SURFACE_DEBUG_VK_FORMAT(R16G16B16_UINT)                                                          \
  SURFACE_DEBUG_VK_FORMAT(R16G16B16_SINT)                                                          \
  SURFACE_DEBUG_VK_FORMAT(R16G16B16_SFLOAT)                                                        \
  SURFACE_DEBUG_VK_FORMAT(R16G16B16A16_UNORM)                                                      \
  SURFACE_DEBUG_VK_FORMAT(R16G16B16A16_SNORM)                                                      \
  SURFACE_DEBUG_VK_FORMAT(R16G16B16A16_USCALED)                                                    \
  SURFACE_DEBUG_VK_FORMAT(R16G16B16A16_SSCALED)                                                    \
  SURFACE_DEBUG_VK_FORMAT(R16G16B16A16_UINT)                                                       \
  SURFACE_DEBUG_VK_FORMAT(R16G16B16A16_SINT)                                                       \
  SURFACE_DEBUG_VK_FORMAT(R16G16B16A16_SFLOAT)                                                     \
  SURFACE_DEBUG_VK_FORMAT(R32_UINT)                                                                \
  SURFACE_DEBUG_VK_FORMAT(R32_SINT)                                                                \
  SURFACE_DEBUG_VK_FORMAT(R32_SFLOAT)                                                              \
  SURFACE_DEBUG_VK_FORMAT(R32G32_UINT)                                                             \
  SURFACE_DEBUG_VK_FORMAT(R32G32_SINT)                                                             \
  SURFACE_DEBUG_VK_FORMAT(R32G32_SFLOAT)                                                           \
  SURFACE_DEBUG_VK_FORMAT(R32G32B32_UINT)                                                          \
  SURFACE_DEBUG_VK_FORMAT(R32G32B32_SINT)                                                          \
  SURFACE_DEBUG_VK_FORMAT(R32G32B32_SFLOAT)                                                        \
  SURFACE_DEBUG_VK_FORMAT(R32G32B32A32_UINT)                                                       \
  SURFACE_DEBUG_VK_FORMAT(R32G32B32A32_SINT)                                                       \
  SURFACE_DEBUG_VK_FORMAT(R32G32B32A32_SFLOAT)                                                     \
  SURFACE_DEBUG_VK_FORMAT(R64_UINT)                                                                \
  SURFACE_DEBUG_VK_FORMAT(R64_SINT)                                                                \
  SURFACE_DEBUG_VK_FORMAT(R64_SFLOAT)                                                              \
  SURFACE_DEBUG_VK_FORMAT(R64G64_UINT)                                                             \
  SURFACE_DEBUG_VK_FORMAT(R64G64_SINT)                                                             \
  SURFACE_DEBUG_VK_FORMAT(R64G64_SFLOAT)                                                           \
  SURFACE_DEBUG_VK_FORMAT(R64G64B64_UINT)                                                          \
  SURFACE_DEBUG_VK_FORMAT(R64G64B64_SINT)                                                          \
  SURFACE_DEBUG_VK_FORMAT(R64G64B64_SFLOAT)                                                        \
  SURFACE_DEBUG_VK_FORMAT(R64G64B64A64_UINT)                                                       \
  SURFACE_DEBUG_VK_FORMAT(R64G64B64A64_SINT)                                                       \
  SURFACE_DEBUG_VK_FORMAT(R64G64B64A64_SFLOAT)                                                     \
  SURFACE_DEBUG_VK_FORMAT(B10G11R11_UFLOAT_PACK32)                                                 \
  SURFACE_DEBUG_VK_FORMAT(E5B9G9R9_UFLOAT_PACK32)                                                  \
  SURFACE_DEBUG_VK_FORMAT(D16_UNORM)                                                               \
  SURFACE_DEBUG_VK_FORMAT(X8_D24_UNORM_PACK32)                                                     \
  SURFACE_DEBUG_VK_FORMAT(D32_SFLOAT)                                                              \
  SURFACE_DEBUG_VK_FORMAT(S8_UINT)                                                                 \
  SURFACE_DEBUG_VK_FORMAT(D16_UNORM_S8_UINT)                                                       \
  SURFACE_DEBUG_VK_FORMAT(D24_UNORM_S8_UINT)                                                       \
  SURFACE_DEBUG_VK_FORMAT(D32_SFLOAT_S8_UINT)                                                      \
  SURFACE_DEBUG_VK_FORMAT(BC1_RGB_UNORM_BLOCK)                                                     \
  SURFACE_DEBUG_VK_FORMAT(BC1_RGB_SRGB_BLOCK)                                                      \
  SURFACE_DEBUG_VK_FORMAT(BC1_RGBA_UNORM_BLOCK)                                                    \
  SURFACE_DEBUG_VK_FORMAT(BC1_RGBA_SRGB_BLOCK)                                                     \
  SURFACE_DEBUG_VK_FORMAT(BC2_UNORM_BLOCK)                                                         \
  SURFACE_DEBUG_VK_FORMAT(BC2_SRGB_BLOCK)                                                          \
  SURFACE_DEBUG_VK_FORMAT(BC3_UNORM_BLOCK)                                                         \
  SURFACE_DEBUG_VK_FORMAT(BC3_SRGB_BLOCK)                                                          \
  SURFACE_DEBUG_VK_FORMAT(BC4_UNORM_BLOCK)                                                         \
  SURFACE_DEBUG_VK_FORMAT(BC4_SNORM_BLOCK)                                                         \
  SURFACE_DEBUG_VK_FORMAT(BC5_UNORM_BLOCK)                                                         \
  SURFACE_DEBUG_VK_FORMAT(BC5_SNORM_BLOCK)                                                         \
  SURFACE_DEBUG_VK_FORMAT(BC6H_UFLOAT_BLOCK)                                                       \
  SURFACE_DEBUG_VK_FORMAT(BC6H_SFLOAT_BLOCK)                                                       \
  SURFACE_DEBUG_VK_FORMAT(BC7_UNORM_BLOCK)                                                         \
  SURFACE_DEBUG_VK_FORMAT(BC7_SRGB_BLOCK)                                                          \
  SURFACE_DEBUG_VK_FORMAT(ETC2_R8G8B8_UNORM_BLOCK)                                                 \
  SURFACE_DEBUG_VK_FORMAT(ETC2_R8G8B8_SRGB_BLOCK)                                                  \
  SURFACE_DEBUG_VK_FORMAT(ETC2_R8G8B8A1_UNORM_BLOCK)                                               \
  SURFACE_DEBUG_VK_FORMAT(ETC2_R8G8B8A1_SRGB_BLOCK)                                                \
  SURFACE_DEBUG_VK_FORMAT(ETC2_R8G8B8A8_UNORM_BLOCK)                                               \
  SURFACE_DEBUG_VK_FORMAT(ETC2_R8G8B8A8_SRGB_BLOCK)                                                \
  SURFACE_DEBUG_VK_FORMAT(EAC_R11_UNORM_BLOCK)                                                     \
  SURFACE_DEBUG_VK_FORMAT(EAC_R11_SNORM_BLOCK)                                                     \
  SURFACE_DEBUG_VK_FORMAT(EAC_R11G11_UNORM_BLOCK)                                                  \
  SURFACE_DEBUG_VK_FORMAT(EAC_R11G11_SNORM_BLOCK)                                                  \
  SURFACE_DEBUG_VK_FORMAT(ASTC_4x4_UNORM_BLOCK)                                                    \
  SURFACE_DEBUG_VK_FORMAT(ASTC_4x4_SRGB_BLOCK)                                                     \
  SURFACE_DEBUG_VK_FORMAT(ASTC_5x4_UNORM_BLOCK)                                                    \
  SURFACE_DEBUG_VK_FORMAT(ASTC_5x4_SRGB_BLOCK)                                                     \
  SURFACE_DEBUG_VK_FORMAT(ASTC_5x5_UNORM_BLOCK)                                                    \
  SURFACE_DEBUG_VK_FORMAT(ASTC_5x5_SRGB_BLOCK)                                                     \
  SURFACE_DEBUG_VK_FORMAT(ASTC_6x5_UNORM_BLOCK)                                                    \
  SURFACE_DEBUG_VK_FORMAT(ASTC_6x5_SRGB_BLOCK)                                                     \
  SURFACE_DEBUG_VK_FORMAT(ASTC_6x6_UNORM_BLOCK)                                                    \
  SURFACE_DEBUG_VK_FORMAT(ASTC_6x6_SRGB_BLOCK)                                                     \
  SURFACE_DEBUG_VK_FORMAT(ASTC_8x5_UNORM_BLOCK)                                                    \
  SURFACE_DEBUG_VK_FORMAT(ASTC_8x5_SRGB_BLOCK)                                                     \
  SURFACE_DEBUG_VK_FORMAT(ASTC_8x6_UNORM_BLOCK)                                                    \
  SURFACE_DEBUG_VK_FORMAT(ASTC_8x6_SRGB_BLOCK)                                                     \
  SURFACE_DEBUG_VK_FORMAT(ASTC_8x8_UNORM_BLOCK)                                                    \
  SURFACE_DEBUG_VK_FORMAT(ASTC_8x8_SRGB_BLOCK)                                                     \
  SURFACE_DEBUG_VK_FORMAT(ASTC_10x5_UNORM_BLOCK)                                                   \
  SURFACE_DEBUG_VK_FORMAT(ASTC_10x5_SRGB_BLOCK)                                                    \
  SURFACE_DEBUG_VK_FORMAT(ASTC_10x6_UNORM_BLOCK)                                                   \
  SURFACE_DEBUG_VK_FORMAT(ASTC_10x6_SRGB_BLOCK)                                                    \
  SURFACE_DEBUG_VK_FORMAT(ASTC_10x8_UNORM_BLOCK)                                                   \
  SURFACE_DEBUG_VK_FORMAT(ASTC_10x8_SRGB_BLOCK)                                                    \
  SURFACE_DEBUG_VK_FORMAT(ASTC_10x10_UNORM_BLOCK)                                                  \
  SURFACE_DEBUG_VK_FORMAT(ASTC_10x10_SRGB_BLOCK)                                                   \
  SURFACE_DEBUG_VK_FORMAT(ASTC_12x10_UNORM_BLOCK)                                                  \
  SURFACE_DEBUG_VK_FORMAT(ASTC_12x10_SRGB_BLOCK)                                                   \
  SURFACE_DEBUG_VK_FORMAT(ASTC_12x12_UNORM_BLOCK)                                                  \
  SURFACE_DEBUG_VK_FORMAT(ASTC_12x12_SRGB_BLOCK)

//
//
//

#undef SURFACE_DEBUG_VK_FORMAT
#define SURFACE_DEBUG_VK_FORMAT(key_) { .val = VK_FORMAT_##key_, .str = "VK_FORMAT_" #key_ },

static struct val_str const surface_debug_vk_formats[] = { SURFACE_DEBUG_VK_FORMATS() };

//
//
//

#define SURFACE_DEBUG_VK_COLOR_SPACES()                                                            \
  SURFACE_DEBUG_VK_COLOR_SPACE(SRGB_NONLINEAR_KHR)                                                 \
  SURFACE_DEBUG_VK_COLOR_SPACE(DISPLAY_P3_NONLINEAR_EXT)                                           \
  SURFACE_DEBUG_VK_COLOR_SPACE(EXTENDED_SRGB_LINEAR_EXT)                                           \
  SURFACE_DEBUG_VK_COLOR_SPACE(DISPLAY_P3_LINEAR_EXT)                                              \
  SURFACE_DEBUG_VK_COLOR_SPACE(DCI_P3_NONLINEAR_EXT)                                               \
  SURFACE_DEBUG_VK_COLOR_SPACE(BT709_LINEAR_EXT)                                                   \
  SURFACE_DEBUG_VK_COLOR_SPACE(BT709_NONLINEAR_EXT)                                                \
  SURFACE_DEBUG_VK_COLOR_SPACE(BT2020_LINEAR_EXT)                                                  \
  SURFACE_DEBUG_VK_COLOR_SPACE(HDR10_ST2084_EXT)                                                   \
  SURFACE_DEBUG_VK_COLOR_SPACE(DOLBYVISION_EXT)                                                    \
  SURFACE_DEBUG_VK_COLOR_SPACE(HDR10_HLG_EXT)                                                      \
  SURFACE_DEBUG_VK_COLOR_SPACE(ADOBERGB_LINEAR_EXT)                                                \
  SURFACE_DEBUG_VK_COLOR_SPACE(ADOBERGB_NONLINEAR_EXT)                                             \
  SURFACE_DEBUG_VK_COLOR_SPACE(PASS_THROUGH_EXT)                                                   \
  SURFACE_DEBUG_VK_COLOR_SPACE(EXTENDED_SRGB_NONLINEAR_EXT)                                        \
  SURFACE_DEBUG_VK_COLOR_SPACE(DISPLAY_NATIVE_AMD)

//
//
//

#undef SURFACE_DEBUG_VK_COLOR_SPACE
#define SURFACE_DEBUG_VK_COLOR_SPACE(key_)                                                         \
  { .val = VK_COLOR_SPACE_##key_, .str = "VK_COLOR_SPACE_" #key_ },

static struct val_str const surface_debug_vk_color_spaces[] = { SURFACE_DEBUG_VK_COLOR_SPACES() };

//
//
//

#define SURFACE_DEBUG_VK_PRESENT_MODES()                                                           \
  SURFACE_DEBUG_VK_PRESENT_MODE(IMMEDIATE_KHR)                                                     \
  SURFACE_DEBUG_VK_PRESENT_MODE(MAILBOX_KHR)                                                       \
  SURFACE_DEBUG_VK_PRESENT_MODE(FIFO_KHR)                                                          \
  SURFACE_DEBUG_VK_PRESENT_MODE(FIFO_RELAXED_KHR)                                                  \
  SURFACE_DEBUG_VK_PRESENT_MODE(SHARED_DEMAND_REFRESH_KHR)                                         \
  SURFACE_DEBUG_VK_PRESENT_MODE(SHARED_CONTINUOUS_REFRESH_KHR)

//
//
//

#undef SURFACE_DEBUG_VK_PRESENT_MODE
#define SURFACE_DEBUG_VK_PRESENT_MODE(key_)                                                        \
  { .val = VK_PRESENT_MODE_##key_, .str = "VK_PRESENT_MODE_" #key_ },

static struct val_str const surface_debug_vk_present_modes[] = { SURFACE_DEBUG_VK_PRESENT_MODES() };

//
//
//

#define SURFACE_DEBUG_VK_SURFACE_TRANSFORMS()                                                      \
  SURFACE_DEBUG_VK_SURFACE_TRANSFORM(IDENTITY_BIT_KHR)                                             \
  SURFACE_DEBUG_VK_SURFACE_TRANSFORM(ROTATE_90_BIT_KHR)                                            \
  SURFACE_DEBUG_VK_SURFACE_TRANSFORM(ROTATE_180_BIT_KHR)                                           \
  SURFACE_DEBUG_VK_SURFACE_TRANSFORM(ROTATE_270_BIT_KHR)                                           \
  SURFACE_DEBUG_VK_SURFACE_TRANSFORM(HORIZONTAL_MIRROR_BIT_KHR)                                    \
  SURFACE_DEBUG_VK_SURFACE_TRANSFORM(HORIZONTAL_MIRROR_ROTATE_90_BIT_KHR)                          \
  SURFACE_DEBUG_VK_SURFACE_TRANSFORM(HORIZONTAL_MIRROR_ROTATE_180_BIT_KHR)                         \
  SURFACE_DEBUG_VK_SURFACE_TRANSFORM(HORIZONTAL_MIRROR_ROTATE_270_BIT_KHR)                         \
  SURFACE_DEBUG_VK_SURFACE_TRANSFORM(INHERIT_BIT_KHR)

//
//
//

#define SURFACE_DEBUG_VK_COMPOSITE_ALPHAS()                                                        \
  SURFACE_DEBUG_VK_COMPOSITE_ALPHA(OPAQUE_BIT_KHR)                                                 \
  SURFACE_DEBUG_VK_COMPOSITE_ALPHA(PRE_MULTIPLIED_BIT_KHR)                                         \
  SURFACE_DEBUG_VK_COMPOSITE_ALPHA(POST_MULTIPLIED_BIT_KHR)                                        \
  SURFACE_DEBUG_VK_COMPOSITE_ALPHA(INHERIT_BIT_KHR)

//
//
//

#define SURFACE_DEBUG_VK_IMAGE_USAGES()                                                            \
  SURFACE_DEBUG_VK_IMAGE_USAGE(TRANSFER_SRC_BIT)                                                   \
  SURFACE_DEBUG_VK_IMAGE_USAGE(TRANSFER_DST_BIT)                                                   \
  SURFACE_DEBUG_VK_IMAGE_USAGE(SAMPLED_BIT)                                                        \
  SURFACE_DEBUG_VK_IMAGE_USAGE(STORAGE_BIT)                                                        \
  SURFACE_DEBUG_VK_IMAGE_USAGE(COLOR_ATTACHMENT_BIT)                                               \
  SURFACE_DEBUG_VK_IMAGE_USAGE(DEPTH_STENCIL_ATTACHMENT_BIT)                                       \
  SURFACE_DEBUG_VK_IMAGE_USAGE(TRANSIENT_ATTACHMENT_BIT)                                           \
  SURFACE_DEBUG_VK_IMAGE_USAGE(INPUT_ATTACHMENT_BIT)                                               \
  SURFACE_DEBUG_VK_IMAGE_USAGE(SHADING_RATE_IMAGE_BIT_NV)                                          \
  SURFACE_DEBUG_VK_IMAGE_USAGE(FRAGMENT_DENSITY_MAP_BIT_EXT)

//
//
//

static int
surface_debug_find_cmp(const void * a, const void * b)
{
  struct val_str const * const vs_a = a;
  struct val_str const * const vs_b = b;

  if (vs_a->val < vs_b->val)
    {
      return -1;
    }
  else if (vs_a->val > vs_b->val)
    {
      return +1;
    }
  else
    {
      return 0;
    }
}

static char const *
surface_debug_find(struct val_str const * a, size_t a_count, uint32_t const * v)
{
  struct val_str const * res = bsearch(v, a, a_count, sizeof(*a), surface_debug_find_cmp);

  if (res != NULL)
    {
      return res->str;
    }
  else
    {
      return "Key not found";
    }
}

#define SURFACE_DEBUG_FIND(a_, v_) surface_debug_find(a_, ARRAY_LENGTH_MACRO(a_), v_)

//
//
//

void
surface_debug_surface_formats(FILE *                     file,
                              uint32_t                   surface_format_count,
                              VkSurfaceFormatKHR const * surface_formats)
{
  fprintf(file, "VkSurfaceFormatKHR[%u]: {\n", surface_format_count);

  for (uint32_t ii = 0; ii < surface_format_count; ii++)
    {
      VkSurfaceFormatKHR const * const sf = surface_formats + ii;

      char const * const f  = SURFACE_DEBUG_FIND(surface_debug_vk_formats, &sf->format);
      char const * const cs = SURFACE_DEBUG_FIND(surface_debug_vk_color_spaces, &sf->colorSpace);

      fprintf(file, "\t{ %-36s, %-42s }\n", f, cs);
    }

  fprintf(file, "}\n");
}

//
//
//

void
surface_debug_image_view_format(FILE * file, VkFormat image_view_format)
{
  fprintf(file, "VkImageViewCreateInfo.format: {\n");

  char const * const f = SURFACE_DEBUG_FIND(surface_debug_vk_formats, &image_view_format);

  fprintf(file, "\t%s\n", f);

  fprintf(file, "}\n");
}

//
//
//

void
surface_debug_surface_transforms(FILE * file, VkSurfaceTransformFlagsKHR transform_flags)
{
  fprintf(file, "{\n");

#undef SURFACE_DEBUG_VK_SURFACE_TRANSFORM
#define SURFACE_DEBUG_VK_SURFACE_TRANSFORM(key_)                                                   \
  if (transform_flags & VK_SURFACE_TRANSFORM_##key_)                                               \
    fprintf(file, "\t%s\n", "VK_SURFACE_TRANSFORM_" #key_);

  SURFACE_DEBUG_VK_SURFACE_TRANSFORMS()

  fprintf(file, "}\n");
}

//
//
//

void
surface_debug_composite_alphas(FILE * file, VkCompositeAlphaFlagsKHR alpha_flags)
{
  fprintf(file, "{\n");

#undef SURFACE_DEBUG_VK_COMPOSITE_ALPHA
#define SURFACE_DEBUG_VK_COMPOSITE_ALPHA(key_)                                                     \
  if (alpha_flags & VK_COMPOSITE_ALPHA_##key_)                                                     \
    fprintf(file, "\t%s\n", "VK_COMPOSITE_ALPHA_" #key_);

  SURFACE_DEBUG_VK_COMPOSITE_ALPHAS()

  fprintf(file, "}\n");
}

//
//
//

void
surface_debug_image_usages(FILE * file, VkImageUsageFlags usage_flags)
{
  fprintf(file, "{\n");

#undef SURFACE_DEBUG_VK_IMAGE_USAGE
#define SURFACE_DEBUG_VK_IMAGE_USAGE(key_)                                                         \
  if (usage_flags & VK_IMAGE_USAGE_##key_)                                                         \
    fprintf(file, "\t%s\n", "VK_IMAGE_USAGE_" #key_);

  SURFACE_DEBUG_VK_IMAGE_USAGES()

  fprintf(file, "}\n");
}

//
//
//

void
surface_debug_surface_capabilities(FILE * file, VkSurfaceCapabilitiesKHR const * const sc)
{
  fprintf(file,
          "VkSurfaceCapabilitiesKHR.minImageCount:\n"
          "\t%u\n",
          sc->minImageCount);

  fprintf(file, "VkSurfaceCapabilitiesKHR.supportedTransforms: ");
  surface_debug_surface_transforms(file, sc->supportedTransforms);

  fprintf(file, "VkSurfaceCapabilitiesKHR.currentTransform: ");
  surface_debug_surface_transforms(file, (VkSurfaceTransformFlagsKHR)sc->currentTransform);

  fprintf(file, "VkSurfaceCapabilitiesKHR.supportedCompositeAlpha: ");
  surface_debug_composite_alphas(file, sc->supportedCompositeAlpha);

  fprintf(file, "VkSurfaceCapabilitiesKHR.supportedUsageFlags: ");
  surface_debug_image_usages(file, sc->supportedUsageFlags);
}

//
//
//

void
surface_debug_surface_present_modes(FILE *                   file,
                                    uint32_t                 present_mode_count,
                                    VkPresentModeKHR const * present_modes)
{
  fprintf(file, "VkPresentModeKHR[%u]: {\n", present_mode_count);

  for (uint32_t ii = 0; ii < present_mode_count; ii++)
    {
      char const * const pm = SURFACE_DEBUG_FIND(surface_debug_vk_present_modes,  //
                                                 present_modes + ii);

      fprintf(file, "\t%s\n", pm);
    }

  fprintf(file, "}\n");
}

//
//
//

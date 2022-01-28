// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "spinel/platforms/vk/ext/find_target/find_target.h"

#include <stdio.h>
#include <stdlib.h>

#include "spinel/platforms/vk/spinel_vk_types.h"

//
// Spinel/VK targets
//
#ifdef SPN_VK_TARGET_ARCHIVE_LINKABLE

#ifdef SPN_VK_TARGET_ARCHIVE_AMD_GCN3
#include "spinel_vk_amd_gcn3_linkable.h"
#endif
#ifdef SPN_VK_TARGET_ARM_BIFROST4
#include "spinel_vk_arm_bifrost4_linkable.h"
#endif
#ifdef SPN_VK_TARGET_ARM_BIFROST8
#include "spinel_vk_arm_bifrost8_linkable.h"
#endif
#ifdef SPN_VK_TARGET_INTEL_GEN8
#include "spinel_vk_intel_gen8_linkable.h"
#endif
#ifdef SPN_VK_TARGET_NVIDIA_SM35
#include "spinel_vk_nvidia_sm35_linkable.h"
#endif
#ifdef SPN_VK_TARGET_NVIDIA_SM75
#include "spinel_vk_nvidia_sm75_linkable.h"
#endif

#endif

//
// NVIDIA fp16 support appears to be in the range [0x1D81, ...]
//
// TODO(allanmac): Add support for identifying NVIDIA Tegra SoCs.
//
#define SPN_VK_TARGET_NVIDIA_GV100 0x1D81

//
//
//
#ifndef NDEBUG
#define SPN_VK_TARGET_LOG(name_) fprintf(stderr, "Loading Spinel target: \"" name_ "\"\n");
#else
#define SPN_VK_TARGET_LOG(name_)
#endif

//
//
//
#define SPN_VK_TARGET_STRINGIFY_2(name_) #name_

#define SPN_VK_TARGET_STRINGIFY(name_) SPN_VK_TARGET_STRINGIFY_2(name_)

//
//
//
union spinel_vk_header_target
{
  struct target_archive_header const * header;  // linkable?
  spinel_vk_target_t *                 target;  // resource?
};

//
// LINKABLE?
//
#ifdef SPN_VK_TARGET_ARCHIVE_LINKABLE

#define SPN_VK_TARGET_GET(name_)                                                                   \
  (union spinel_vk_header_target)                                                                  \
  {                                                                                                \
    .header = name_##_linkable                                                                     \
  }

#define SPN_VK_TARGET_ASSIGN(header_, name_)                                                       \
  SPN_VK_TARGET_LOG(SPN_VK_TARGET_STRINGIFY(name_));                                               \
  header_ = SPN_VK_TARGET_GET(name_)

//
// RESOURCE?
//
// Load the target binary.
//
// Must be freed by caller.
//
#else

static spinel_vk_target_t *
spinel_vk_load_target(char const * filename)
{
  FILE * file = fopen(filename, "r");

  if (file == NULL)
    {
      fprintf(stderr, "Error: Can't open target filename \"%s\"\n", filename);
      return NULL;
    }

  if (fseek(file, 0, SEEK_END) != 0)
    {
      fprintf(stderr, "Error: Can't seek target filename \"%s\"\n", filename);
      fclose(file);
      return NULL;
    }

  long int const file_size = ftell(file);

  if (file_size == -1L)
    {
      fprintf(stderr, "Error: Can't determine size of target filename \"%s\"\n", filename);
      fclose(file);
      return NULL;
    }

  if (fseek(file, 0, SEEK_SET) != 0)
    {
      fprintf(stderr, "Error: Can't seek target filename \"%s\"\n", filename);
      fclose(file);
      return NULL;
    }

  struct spinel_vk_target * target = malloc(file_size);

  if (fread(target, 1, file_size, file) != (size_t)file_size)
    {
      fprintf(stderr, "Error: Can't read target filename \"%s\"\n", filename);
      fclose(file);
      free(target);
      return NULL;
    }

  fclose(file);

  return target;
}

// clang-format off
#define SPN_VK_TARGET_FILENAME(name_)        "pkg/data/targets/" SPN_VK_TARGET_STRINGIFY(name_) "_resource.ar"
#define SPN_VK_TARGET_LOAD(name_)            spinel_vk_load_target(SPN_VK_TARGET_FILENAME(name_))
#define SPN_VK_TARGET_GET(name_)             (union spinel_vk_header_target){ .target = SPN_VK_TARGET_LOAD(name_) }
#define SPN_VK_TARGET_ASSIGN(header_, name_) SPN_VK_TARGET_LOG(SPN_VK_TARGET_FILENAME(name_)); header_ = SPN_VK_TARGET_GET(name_)
// clang-format on

#endif

//
// Find a Fuchsia loadable target by vendor/device id
//
spinel_vk_target_t *
spinel_vk_find_target(uint32_t vendor_id, uint32_t device_id)
{
  union spinel_vk_header_target header_target = { .header = NULL };

  switch (vendor_id)
    {
#if defined(SPN_VK_TARGET_NVIDIA_SM35) || defined(SPN_VK_TARGET_NVIDIA_SM75)
      case 0x10DE:
        // clang-format off
        //
        // NVIDIA
        //
        // For a mapping of PCI IDs to NVIDIA architectures:
        //
        //  * https://pci-ids.ucw.cz/read/PC/10de
        //  * https://github.com/envytools/envytools/
        //
        // For discrete NVIDIA GPUs, it appears that any PCI ID greater than or
        // equal to "0x1D81" (GV100 [TITAN V]) has full-rate fp16 support.
        //
        // TODO(allanmac): Add support for NVIDIA Tegra SoCs.
        //
        // clang-format on
        //
        if (device_id >= SPN_VK_TARGET_NVIDIA_GV100)  // GV100 [TITAN V], Turing, Ampere+
          {
            SPN_VK_TARGET_ASSIGN(header_target, spinel_vk_nvidia_sm75);
          }
        else  // otherwise, assume no fp16 support
          {
            SPN_VK_TARGET_ASSIGN(header_target, spinel_vk_nvidia_sm35);
          }
        break;
#endif
#ifdef SPN_VK_TARGET_AMD_GCN3
      case 0x1002:
        //
        // AMD GCN
        //
        // FIXME(allanmac): Assumes 64-wide subgroup which are supported by
        // both GCN* and RDNA*.  At some point we should add an RDNA-tuned
        // target.
        //
        SPN_VK_TARGET_ASSIGN(header_target, spinel_vk_amd_gcn3);
        break;
#endif
#ifdef SPN_VK_TARGET_INTEL_GEN8
      case 0x8086:
        //
        // INTEL
        //
        // FIXME(allanmac): for now, the shaders in this app are targeting
        // GEN8+ devices -- this does *not* include variants of GEN9LP+
        // "Apollo Lake" because that device has a different architectural
        // "shape" than GEN8 GTx.  You could add some rigorous rejection by
        // device id here...
        //
        SPN_VK_TARGET_ASSIGN(header_target, spinel_vk_intel_gen8);
        break;
#endif
      case 0x13B5:
        //
        // ARM MALI
        //
        switch (device_id)
          {
#ifdef SPN_VK_TARGET_ARM_BIFROST4
            case 0x70930000:
              //
              // ARM BIFROST4
              //
              SPN_VK_TARGET_ASSIGN(header_target, spinel_vk_arm_bifrost4);
              break;
#endif
#ifdef SPN_VK_TARGET_ARM_BIFROST8
            case 0x72120000:
              //
              // ARM BIFROST8
              //
              SPN_VK_TARGET_ASSIGN(header_target, spinel_vk_arm_bifrost8);
              break;
#endif
            default:
              break;
          }
        break;

      default:
        break;
    }

  return header_target.target;
}

//
//
//
void
spinel_vk_target_dispose(spinel_vk_target_t * target)
{
#ifdef SPN_VK_TARGET_ARCHIVE_LINKABLE
  ;  // Nothing to do
#else
  free((void *)target);
#endif
}

//
//
//

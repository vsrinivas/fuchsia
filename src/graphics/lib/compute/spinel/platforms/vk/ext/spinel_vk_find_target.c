// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "spinel_vk_find_target.h"

#include <stdio.h>

//
// Spinel targets
//

#include "targets/vendors/amd/gcn3/hotsort/hs_target.h"
#include "targets/vendors/amd/gcn3/spn_target.h"
#include "targets/vendors/intel/gen8/hotsort/hs_target.h"
#include "targets/vendors/intel/gen8/spn_target.h"
#include "targets/vendors/nvidia/sm50/hotsort/hs_target.h"
#include "targets/vendors/nvidia/sm50/spn_target.h"

//
//
//

bool
spn_vk_find_target(uint32_t const                          vendor_id,
                   uint32_t const                          device_id,
                   struct spn_vk_target const ** const     spinel_target,
                   struct hotsort_vk_target const ** const hotsort_target,
                   char * const                            error_buffer,
                   size_t const                            error_buffer_size)
{
  switch (vendor_id)
    {
        case 0x10DE:  // NVIDIA
        {
          //
          // FIXME -- for now, the kernels in this app are targeting
          // sm_35+ devices.  You could add some rigorous rejection by
          // device id here...
          //
          *spinel_target  = spn_nvidia_sm50;
          *hotsort_target = hs_nvidia_sm35_u64;
          return true;
        }
        case 0x8086:  // INTEL
        {
          //
          // FIXME -- for now, the kernels in this app are targeting GEN8+
          // devices -- this does *not* include variants of GEN9LP+
          // "Apollo Lake" because that device has a different
          // architectural "shape" than GEN8 GTx.  You could add some
          // rigorous rejection by device id here...
          //
          *spinel_target  = spn_intel_gen8;
          *hotsort_target = hs_intel_gen8_u64;
          return true;
        }
        case 0x1002:  // AMD
        {
          //
          // AMD GCN
          //
          *spinel_target  = spn_amd_gcn3;
          *hotsort_target = hs_amd_gcn3_u64;
          return true;
        }
        case 0x13B5: {
          //
          // ARM BIFROST
          //
          if (device_id == 0x1234)
            {
              //
              // BIFROST GEN1 - subgroupSize = 4
              //
              snprintf(error_buffer, error_buffer_size, "Detected Bitforst4...");
              return false;  // spn_arm_bifrost4;
            }
          else if (device_id == 0x5678)
            {
              //
              // BIFROST GEN2 - subgroupSize = 8
              //
              snprintf(error_buffer, error_buffer_size, "Detected Bitfrost8...");
              return false;  // spn_arm_bifrost8;
            }
        }
      default:;
    }
  snprintf(error_buffer,
           error_buffer_size,
           "No spinel configuration data for (vendor=%X, device=%X)",
           vendor_id,
           device_id);
  return false;
}

//
//
//

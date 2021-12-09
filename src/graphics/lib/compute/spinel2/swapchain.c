// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "swapchain.h"

#include <assert.h>
#include <memory.h>

#include "core_c.h"
#include "spinel/spinel.h"
#include "spinel/spinel_opcodes.h"

//
//
//

spinel_result_t
spinel_swapchain_retain(spinel_swapchain_t swapchain)
{
  assert(swapchain->ref_count >= 1);

  ++swapchain->ref_count;

  return SPN_SUCCESS;
}

spinel_result_t
spinel_swapchain_release(spinel_swapchain_t swapchain)
{
  assert(swapchain->ref_count >= 1);

  if (--swapchain->ref_count == 0)
    {
      return swapchain->release(swapchain->impl);
    }
  else
    {
      return SPN_SUCCESS;
    }
}

spinel_result_t
spinel_swapchain_submit(spinel_swapchain_t swapchain, spinel_swapchain_submit_t const * submit)
{
  return swapchain->submit(swapchain->impl, submit);
}

//
//
//

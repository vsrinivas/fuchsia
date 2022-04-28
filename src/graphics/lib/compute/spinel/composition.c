// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//
//
//

#include "composition.h"

#include <assert.h>

//
// high level composition object
//

spinel_result_t
spinel_composition_retain(spinel_composition_t composition)
{
  assert(composition->ref_count >= 1);

  ++composition->ref_count;

  return SPN_SUCCESS;
}

spinel_result_t
spinel_composition_release(spinel_composition_t composition)
{
  assert(composition->ref_count >= 1);

  if (--composition->ref_count == 0)
    {
      return composition->release(composition->impl);
    }
  else
    {
      return SPN_SUCCESS;
    }
}

//
//
//

spinel_result_t
spinel_composition_place(spinel_composition_t    composition,
                         spinel_raster_t const * rasters,
                         spinel_layer_id const * layer_ids,
                         spinel_txty_t const *   txtys,
                         uint32_t                count)
{
  return composition->place(composition->impl, rasters, layer_ids, txtys, count);
}

//
//
//

spinel_result_t
spinel_composition_seal(spinel_composition_t composition)
{
  //
  // seal the composition
  //
  return composition->seal(composition->impl);
}

//
//
//

spinel_result_t
spinel_composition_unseal(spinel_composition_t composition)
{
  //
  // unseal the composition
  //
  return composition->unseal(composition->impl);
}

//
//
//

spinel_result_t
spinel_composition_reset(spinel_composition_t composition)
{
  //
  // unseal the composition
  //
  return composition->reset(composition->impl);
}

//
//
//

spinel_result_t
spinel_composition_set_clip(spinel_composition_t composition, spinel_pixel_clip_t const * clip)
{
  return composition->set_clip(composition->impl, clip);
}

//
//
//

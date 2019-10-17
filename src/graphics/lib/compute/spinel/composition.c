// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//
//
//

#include "composition.h"

//
// high level composition object
//

spn_result_t
spn_composition_retain(spn_composition_t composition)
{
  composition->ref_count += 1;

  return SPN_SUCCESS;
}

spn_result_t
spn_composition_release(spn_composition_t composition)
{
  return composition->release(composition->impl);
}

//
//
//

spn_result_t
spn_composition_place(spn_composition_t    composition,
                      spn_raster_t const * rasters,
                      spn_layer_id const * layer_ids,
                      spn_txty_t const *   txtys,
                      uint32_t             count)
{
  return composition->place(composition->impl, rasters, layer_ids, txtys, count);
}

//
//
//

spn_result_t
spn_composition_seal(spn_composition_t composition)
{
  //
  // seal the composition
  //
  return composition->seal(composition->impl);
}

//
//
//

spn_result_t
spn_composition_unseal(spn_composition_t composition)
{
  //
  // unseal the composition
  //
  return composition->unseal(composition->impl);
}

//
//
//

spn_result_t
spn_composition_reset(spn_composition_t composition)
{
  //
  // unseal the composition
  //
  return composition->reset(composition->impl);
}

//
//
//

spn_result_t
spn_composition_clone(spn_context_t       context,
                      spn_composition_t   composition,
                      spn_composition_t * clone)
{
  return composition->clone(composition->impl, clone);
}

//
//
//

spn_result_t
spn_composition_get_bounds(spn_composition_t composition, uint32_t bounds[4])
{
  return composition->get_bounds(composition->impl, bounds);
}

//
//
//

spn_result_t
spn_composition_set_clip(spn_composition_t composition, uint32_t const clip[4])
{
  return composition->set_clip(composition->impl, clip);
}

//
//
//

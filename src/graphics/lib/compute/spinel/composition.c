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

spn_result
spn_composition_retain(spn_composition_t composition)
{
  composition->ref_count += 1;

  return SPN_SUCCESS;
}

spn_result
spn_composition_release(spn_composition_t composition)
{
  return composition->release(composition->impl);
}

//
//
//

spn_result
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

spn_result
spn_composition_unseal(spn_composition_t composition, bool reset)
{
  //
  // unseal the composition
  //
  return composition->unseal(composition->impl,reset);
}

//
//
//

spn_result
spn_composition_get_bounds(spn_composition_t composition, int32_t bounds[4])
{
  return composition->get_bounds(composition->impl,bounds);
}

//
//
//

spn_result
spn_composition_place(spn_composition_t    composition,
                      spn_raster_t const * rasters,
                      spn_layer_id const * layer_ids,
                      float        const * txs,
                      float        const * tys,
                      uint32_t             count) // NOTE: A PER-PLACE CLIP IS POSSIBLE
{
  return composition->place(composition->impl,rasters,layer_ids,txs,tys,count);
}

//
//
//

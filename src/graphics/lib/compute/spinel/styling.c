// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>

#include "spinel.h"
#include "styling.h"
#include "core_c.h"

//
//
//

spn_result
spn_styling_retain(spn_styling_t styling)
{
  styling->ref_count += 1;

  return SPN_SUCCESS;
}

spn_result
spn_styling_release(spn_styling_t styling)
{
  return styling->release(styling->impl);
}

spn_result
spn_styling_seal(spn_styling_t styling)
{
  return styling->seal(styling->impl);
}

spn_result
spn_styling_unseal(spn_styling_t styling)
{
  return styling->unseal(styling->impl);
}

spn_result
spn_styling_reset(spn_styling_t styling)
{
  spn_result const res = styling->unseal(styling->impl);

  if (res)
    return res;

  styling->dwords.next = styling->layers.count * SPN_STYLING_LAYER_COUNT_DWORDS;

  return SPN_SUCCESS;
}

//
// FIXME -- various robustifications can be made to this builder but
// we don't want to make this heavyweight too soon
//
// - out of range layer_id is an error
// - extras[] overflow is an error
//

spn_result
spn_styling_group_alloc(spn_styling_t        styling,
                        spn_group_id * const group_id)
{
  spn_result const res = styling->unseal(styling->impl);

  if (res)
    return res;

  *group_id             = styling->dwords.next;
  styling->dwords.next += SPN_STYLING_GROUP_COUNT_DWORDS;

  return SPN_SUCCESS;
}

spn_result
spn_styling_group_enter(spn_styling_t       styling,
                        spn_group_id  const group_id,
                        uint32_t      const n,
                        uint32_t  * * const cmds)
{
  assert(styling->dwords.next + n <= styling->dwords.count);

  spn_result const res = styling->unseal(styling->impl);

  if (res)
    return res;

  styling->extent[group_id + SPN_STYLING_GROUP_OFFSET_CMDS_ENTER] = styling->dwords.next;

  *cmds = styling->extent + styling->dwords.next;

  styling->dwords.next += n;

  return SPN_SUCCESS;
}

spn_result
spn_styling_group_leave(spn_styling_t       styling,
                        spn_group_id  const group_id,
                        uint32_t      const n,
                        uint32_t  * * const cmds)
{
  assert(styling->dwords.next + n <= styling->dwords.count);

  spn_result const res = styling->unseal(styling->impl);

  if (res)
    return res;

  styling->extent[group_id + SPN_STYLING_GROUP_OFFSET_CMDS_LEAVE] = styling->dwords.next;

  *cmds = styling->extent + styling->dwords.next;

  styling->dwords.next += n;

  return SPN_SUCCESS;
}

spn_result
spn_styling_group_parents(spn_styling_t styling,
                          spn_group_id  const group_id,
                          uint32_t      const n,
                          uint32_t  * * const parents)
{
  assert(styling->dwords.next + n <= styling->dwords.count);

  spn_result const res = styling->unseal(styling->impl);

  if (res)
    return res;

  styling->extent[group_id + SPN_STYLING_GROUP_OFFSET_PARENTS_DEPTH] = n;
  styling->extent[group_id + SPN_STYLING_GROUP_OFFSET_PARENTS_BASE ] = styling->dwords.next;

  *parents = styling->extent + styling->dwords.next;

  styling->dwords.next += n;

  return SPN_SUCCESS;
}

spn_result
spn_styling_group_range_lo(spn_styling_t styling,
                           spn_group_id  const group_id,
                           spn_layer_id  const layer_lo)
{
  assert(layer_lo < styling->layers.count);

  spn_result const res = styling->unseal(styling->impl);

  if (res)
    return res;

  styling->extent[group_id + SPN_STYLING_GROUP_OFFSET_RANGE_LO] = layer_lo;

  return SPN_SUCCESS;
}

spn_result
spn_styling_group_range_hi(spn_styling_t styling,
                           spn_group_id  const group_id,
                           spn_layer_id  const layer_hi)
{
  assert(layer_hi < styling->layers.count);

  spn_result const res = styling->unseal(styling->impl);

  if (res)
    return res;

  styling->extent[group_id + SPN_STYLING_GROUP_OFFSET_RANGE_HI] = layer_hi;

  return SPN_SUCCESS;
}

spn_result
spn_styling_group_layer(spn_styling_t               styling,
                        spn_group_id          const group_id,
                        spn_layer_id          const layer_id,
                        uint32_t              const n,
                        spn_styling_cmd_t * * const cmds)
{
  assert(layer_id < styling->layers.count);
  assert(styling->dwords.next + n <= styling->dwords.count);

  spn_result const res = styling->unseal(styling->impl);

  if (res)
    return res;

  styling->extent[layer_id * SPN_STYLING_LAYER_COUNT_DWORDS + SPN_STYLING_LAYER_OFFSET_CMDS]   = styling->dwords.next;
  styling->extent[layer_id * SPN_STYLING_LAYER_COUNT_DWORDS + SPN_STYLING_LAYER_OFFSET_PARENT] = group_id;

  *cmds = styling->extent + styling->dwords.next;

  styling->dwords.next += n;

  return SPN_SUCCESS;
}

//
//
//

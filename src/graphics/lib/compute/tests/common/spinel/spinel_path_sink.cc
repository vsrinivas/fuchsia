// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tests/common/spinel/spinel_path_sink.h"

#include <spinel/spinel.h>
#include <spinel/spinel_assert.h>

#include "tests/common/utils.h"

SpinelPathSink::SpinelPathSink(spn_context_t context) : context_(context)
{
  spn(path_builder_create(context_, &path_builder_));
  path_builder_owner_ = true;
}

SpinelPathSink::SpinelPathSink(spn_context_t context, spn_path_builder_t path_builder)
    : context_(context), path_builder_(path_builder), path_builder_owner_(false)
{
}

SpinelPathSink::~SpinelPathSink()
{
  reset();

  if (path_builder_owner_)
    spn(path_builder_release(path_builder_));
}

void
SpinelPathSink::begin()
{
  if (has_error_)
    return;

  if (spn_path_builder_begin(path_builder_) != SPN_SUCCESS)
    {
      has_error_ = true;
    }
}

void
SpinelPathSink::addItem(ItemType item_type, const double * coords)
{
  spn_result_t result;
  if (has_error_)
    return;

  switch (item_type)
    {
      case MOVE_TO:
        result = spn_path_builder_move_to(path_builder_, coords[0], coords[1]);
        break;
      case LINE_TO:
        result = spn_path_builder_line_to(path_builder_, coords[0], coords[1]);
        break;
      case QUAD_TO:
        result =
          spn_path_builder_quad_to(path_builder_, coords[0], coords[1], coords[2], coords[3]);
        break;
      case CUBIC_TO:
        result = spn_path_builder_cubic_to(path_builder_,
                                           coords[0],
                                           coords[1],
                                           coords[2],
                                           coords[3],
                                           coords[4],
                                           coords[5]);
        break;
      case RAT_QUAD_TO:
        result = spn_path_builder_rat_quad_to(path_builder_,
                                              coords[0],
                                              coords[1],
                                              coords[2],
                                              coords[3],
                                              coords[4]);
        break;
      case RAT_CUBIC_TO:
        result = spn_path_builder_rat_cubic_to(path_builder_,
                                               coords[0],
                                               coords[1],
                                               coords[2],
                                               coords[3],
                                               coords[4],
                                               coords[5],
                                               coords[6],
                                               coords[7]);
        break;
      default:
        ASSERT_MSG(false, "Unknown path item type: %d\n", item_type);
    }
  if (result != SPN_SUCCESS)
    has_error_ = true;
}

bool
SpinelPathSink::end()
{
  if (has_error_)
    return false;

  spn_path_t path;
  if (spn_path_builder_end(path_builder_, &path) != SPN_SUCCESS)
    {
      has_error_ = true;
      return false;
    }
  paths_.push_back(path);
  return true;
}

void
SpinelPathSink::reset()
{
  spn(path_release(context_, paths(), size()));
  paths_.clear();
}

std::vector<spn_path_t>
SpinelPathSink::release()
{
  std::vector<spn_path_t> result = std::move(paths_);
  paths_.clear();  // just to be safe.
  return result;
}

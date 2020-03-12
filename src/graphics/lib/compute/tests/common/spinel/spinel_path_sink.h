// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_COMPUTE_TESTS_COMMON_SPINEL_SPINEL_PATH_SINK_H_
#define SRC_GRAPHICS_LIB_COMPUTE_TESTS_COMMON_SPINEL_SPINEL_PATH_SINK_H_

#include <spinel/spinel_types.h>

#include <vector>

#include "tests/common/path_sink.h"

// A PathSink implementation that records path items into
// one of more spn_path_t handles. Usage is:
//
//   1) Create instance, passing an spn_context_t handle.
//
//   2) Add paths elements as usual.
//
//   3) Use size() to return the number of created paths, and
//      paths() to return the array of spn_path_t handles.
//
//   4) Use release() to transfer ownership of the path handles
//      to the caller. Otherwise, they are released with a
//      call to reset() or by the destructor.
//
class SpinelPathSink : public PathSink {
 public:
  // Constructor. Takes a non-owning reference to a Spinel context.
  explicit SpinelPathSink(spn_context_t context);

  // Constructor that non-owning references to a Spinel context and a
  // path builder.
  SpinelPathSink(spn_context_t context, spn_path_builder_t path_builder);

  // Destructor.
  ~SpinelPathSink();

  // PathSink overrides.
  void
  begin() override;
  void
  addItem(ItemType item_type, const double * coords) override;
  bool
  end() override;

  // Return number of recorded paths.
  uint32_t
  size() const
  {
    return static_cast<uint32_t>(paths_.size());
  }

  // Return the recorded path handles.
  const spn_path_t *
  paths() const
  {
    return paths_.data();
  }

  // Reset all recorded paths.
  void
  reset();

  // Return ownership of all recorded paths to the caller.
  std::vector<spn_path_t>
  release();

 private:
  spn_context_t           context_;
  spn_path_builder_t      path_builder_       = nullptr;
  bool                    path_builder_owner_ = false;
  bool                    has_error_          = false;
  std::vector<spn_path_t> paths_;
};

#endif  // SRC_GRAPHICS_LIB_COMPUTE_TESTS_COMMON_SPINEL_SPINEL_PATH_SINK_H_

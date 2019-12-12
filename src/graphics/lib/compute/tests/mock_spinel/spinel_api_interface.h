// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_COMPUTE_TESTS_MOCK_SPINEL_SPINEL_API_INTERFACE_H_
#define SRC_GRAPHICS_LIB_COMPUTE_TESTS_MOCK_SPINEL_SPINEL_API_INTERFACE_H_

#include <spinel/spinel.h>

namespace spinel_api {

// The Spinel API decomposed into a set of abstract C++ classes.
// NOTE: This will need to be kept in sync with the rest of the Spinel sources.
//

// IMPORTANT: The associated source file implements the C Spinel API except for
//            the context creation API, and the spn_render() call, which
//            must be provided by the concrete implementation itself.

// Base class for all reference-counted classes below. Provides retain() and
// release() methods.
class RefCounted {
 public:
  RefCounted() = default;

  spn_result_t
  retain()
  {
    ref_count_++;
    return SPN_SUCCESS;
  }

  spn_result_t
  release()
  {
    if (--ref_count_ == 0)
      {
        delete this;
      }
    return SPN_SUCCESS;
  }

 protected:
  virtual ~RefCounted() = default;

 private:
  uint32_t ref_count_ = 1;
};

// Base class to provide helper functions to export an instance pointer
// to a given Spinel-compatible struct type (e.g. spn_context, not
// spn_context_t).
template <class SPINEL_TYPE>
class SpinelWrapper {
 public:
  const SPINEL_TYPE *
  toSpinel() const
  {
    return reinterpret_cast<const SPINEL_TYPE *>(this);
  }
  SPINEL_TYPE *
  toSpinel()
  {
    return reinterpret_cast<SPINEL_TYPE *>(this);
  }
};

// Base wrapper class for spn_context_t.
// NOTE: Creation of a new Context instance / spn_context_t is left to each implementation.
class Context : public RefCounted, public SpinelWrapper<struct spn_context> {
 public:
  virtual spn_result_t
  reset() = 0;
  virtual spn_result_t
  status() const = 0;

  // NOTE: the createXXX() method should set pointers to PathBuilder/RasterBuilder/Composition/Styling
  // instances.
  virtual spn_result_t
  createPathBuilder(spn_path_builder_t *) = 0;
  virtual spn_result_t
  createRasterBuilder(spn_raster_builder_t *) = 0;
  virtual spn_result_t
  createComposition(spn_composition_t *) = 0;
  virtual spn_result_t
  cloneComposition(spn_composition_t, spn_composition_t *) = 0;
  virtual spn_result_t
  createStyling(uint32_t, uint32_t, spn_styling_t *) = 0;

  virtual spn_result_t
  retainPaths(const spn_path_t *, uint32_t) = 0;
  virtual spn_result_t
  releasePaths(const spn_path_t *, uint32_t) = 0;
  virtual spn_result_t
  retainRasters(const spn_raster_t *, uint32_t) = 0;
  virtual spn_result_t
  releaseRasters(const spn_raster_t *, uint32_t) = 0;

  virtual spn_result_t
  render(const spn_render_submit_t *) = 0;
};

// Base wrapper class for spn_path_builder_t.
class PathBuilder : public RefCounted, public SpinelWrapper<struct spn_path_builder> {
 public:
  virtual spn_result_t
  flush() = 0;
  virtual spn_result_t
  begin() = 0;
  virtual spn_result_t
  end(spn_path_t *) = 0;
  virtual spn_result_t
  moveTo(float x0, float y0) = 0;
  virtual spn_result_t
  lineTo(float x0, float y0) = 0;
  virtual spn_result_t
  quadTo(float cx, float cy, float x, float y) = 0;
  virtual spn_result_t
  cubicTo(float c1x, float c1y, float c2x, float c2y, float x, float y) = 0;
  virtual spn_result_t
  ratQuadTo(float cx, float cy, float x, float y, float w) = 0;
  virtual spn_result_t
  ratCubicTo(float c1x, float c1y, float c2x, float c2y, float x, float y, float w1, float w2) = 0;
  virtual spn_result_t
  reset() = 0;
};

// Base wrapper class for spn_raster_builder_t.
class RasterBuilder : public RefCounted, public SpinelWrapper<struct spn_raster_builder> {
 public:
  virtual spn_result_t
  flush() = 0;
  virtual spn_result_t
  begin() = 0;
  virtual spn_result_t
  end(spn_raster_t *) = 0;
  virtual spn_result_t
  add(const spn_path_t *,
      spn_transform_weakref_t *,
      const spn_transform_t *,
      spn_clip_weakref_t *,
      const spn_clip_t *,
      uint32_t) = 0;
};

// Base wrapper class for spn_composition_t.
class Composition : public RefCounted, public SpinelWrapper<struct spn_composition> {
 public:
  virtual Composition *
  clone() = 0;
  virtual spn_result_t
  reset() = 0;
  virtual spn_result_t
  seal() = 0;
  virtual spn_result_t
  unseal() = 0;
  virtual spn_result_t
  place(const spn_raster_t *, const spn_layer_id *, const spn_txty_t *, uint32_t) = 0;
  virtual spn_result_t
  getBounds(uint32_t bounds[4]) const = 0;
  virtual spn_result_t
  setClip(const uint32_t bounds[4]) = 0;
};

// Base wrapper class for spn_styling_t.
class Styling : public RefCounted, public SpinelWrapper<struct spn_styling> {
 public:
  virtual spn_result_t
  reset() = 0;
  virtual spn_result_t
  seal() = 0;
  virtual spn_result_t
  unseal() = 0;
  virtual spn_result_t
  groupAllocId(spn_group_id *) = 0;
  virtual spn_result_t
  groupAllocEnterCommands(spn_group_id, uint32_t, spn_styling_cmd_t **) = 0;
  virtual spn_result_t
  groupAllocLeaveCommands(spn_group_id, uint32_t, spn_styling_cmd_t **) = 0;
  virtual spn_result_t
  groupAllocParents(spn_group_id, uint32_t, spn_group_id **) = 0;
  virtual spn_result_t
                       groupAllocLayerCommands(spn_group_id, spn_layer_id, uint32_t, spn_styling_cmd_t **) = 0;
  virtual spn_result_t groupSetRangeLo(spn_group_id, spn_layer_id) = 0;
  virtual spn_result_t groupSetRangeHi(spn_group_id, spn_layer_id) = 0;
};

struct Interface
{
  virtual ~Interface() = default;

  // Creation of Context object is left to the concrete implementation, which will
  // take its own set of parameters to return a new spn_context_t value, as a pointer
  // to a Context instance.

  // Direct methods of Interface to deal with command encoding.
  virtual void
  encodeCommandFillRgba(spn_styling_cmd_t *, const float rgba[4]) = 0;
  virtual void
  encodeCommandBackgroundOver(spn_styling_cmd_t *, const float rgba[4]) = 0;
};

// Set the global pointer to the spinel api. Return the previous value.
Interface *
SetImplementation(Interface * spinel_api);

}  // namespace spinel_api

#endif  // SRC_GRAPHICS_LIB_COMPUTE_TESTS_MOCK_SPINEL_SPINEL_API_INTERFACE_H_

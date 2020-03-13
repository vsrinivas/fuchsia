// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_COMPUTE_TESTS_COMMON_SVG_SVG_SCENE_H_
#define SRC_GRAPHICS_LIB_COMPUTE_TESTS_COMMON_SVG_SVG_SCENE_H_

#include <cstdint>
#include <vector>

#include "svg/svg.h"
#include "tests/common/affine_transform.h"

// An SvgScene represents a 2D space to draw one or more svg document instances.
// The class supports any mix of svg instances and transforms. Usage is the
// following:
//
//    1) Create instance, or call reset() to clear the scene.
//
//    2) Call one of the addSvgDocument() method to add an SVG document
//       (potentially translated/transformed) to the scene. Repeat as many
//       times as needed.
//
//    3) Call getBounds() to retrieve the bounding box of the current scene.
//
//    4) Call unique_svgs() to retrieve the array of unique svg instance
//       pointers added to the scene. I.e. if the same svg was added several
//       times with different transforms, this will return an array of 1 item
//       only.
//
//    5) Call unique_paths() to retrieve the array of unique paths added to
//       the scene. Each SvgScene::Path references an item from unique_svgs()
//       by its index, and gives the original |path_id| from the corresponding
//       document.
//
//    6) Call unique_rasters() to retrieve the array of unique rasters added
//       to the scene. Each SvgScene::Raster references a unique_svgs() item,
//       and a unique_paths() item by index, as well as the original raster
//       by its document id.
//
//    7) Call layers() to retrieve the list of layers() for this scene.
//       This is the concatenation of all layers from the scene svg documents.
//       Each SvgScene::Layer references one or more unique_rasters() rasters
//       and their placements.
//

class SvgScene {
 public:
  SvgScene();

  ~SvgScene();

  // Add one SVG document at its default location.
  void
  addSvgDocument(const struct svg * svg);

  // Add one SVG document at a given translated coordinate.
  void
  addSvgDocument(const struct svg * svg, double dx, double dy);

  // Add an affinetransformed SVG document to the scene.
  void
  addSvgDocument(const struct svg * svg, const affine_transform_t transform);

  // Reset/clear the scene entirely.
  void
  reset();

  // Return the bounds of the overall scene.
  void
  getBounds(double * xmin, double * ymin, double * xmax, double * ymax);

  // Return the list of unique svg documents in this scene.
  const std::vector<const svg *>
  unique_svgs() const;

  // A small struct to identify a unique path in the scene.
  struct Path
  {
    uint32_t svg_index;
    uint32_t path_id;  // index in original document.
  };

  // Return the list of unique paths in this scene.
  const std::vector<Path> &
  unique_paths() const;

  struct Raster
  {
    uint32_t           svg_index;   // index into unique_svgs().
    uint32_t           raster_id;   // index in original document.
    uint32_t           path_index;  // index into unique_paths().
    affine_transform_t transform;
  };

  // Return the list of unique rasters in this scene.
  const std::vector<Raster> &
  unique_rasters() const;

  // A struct used to identify a layer in the scene.
  struct Print
  {
    uint32_t raster_index;  // into unique_rasters() array.
    int32_t  tx, ty;
  };

  struct Layer
  {
    uint32_t           svg_index;  // index into unique_svgs()
    uint32_t           layer_id;   // global scene layer id.
    svg_color_t        fill_color    = 0;
    double             fill_opacity  = 1.0;
    bool               fill_even_odd = false;
    double             opacity       = 1.0;
    std::vector<Print> prints;
  };

  // Return the list of layers for this scene.
  const std::vector<Layer> &
  layers() const;

  // Rebuild all unique sets if needed. Return true if an update was
  // performed, or false otherwise.
  bool
  ensureUpdated() const
  {
    if (!update_needed_)
      return false;

    update();
    return true;
  }

 protected:
  class Impl;

  void
  invalidate();

  void
  update() const;

  struct Item
  {
    const svg *        svg;
    affine_transform_t transform;
  };

  std::vector<Item> items_;

  mutable bool   update_needed_ = true;
  mutable Impl * impl_          = nullptr;
};

#endif  // SRC_GRAPHICS_LIB_COMPUTE_TESTS_COMMON_SVG_SVG_SCENE_H_

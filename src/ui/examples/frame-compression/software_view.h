// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_EXAMPLES_FRAME_COMPRESSION_SOFTWARE_VIEW_H_
#define SRC_UI_EXAMPLES_FRAME_COMPRESSION_SOFTWARE_VIEW_H_

#include <fuchsia/sysmem/cpp/fidl.h>
#include <lib/inspect/cpp/inspect.h>
#include <lib/inspect/cpp/value_list.h>
#include <lib/inspect/cpp/vmo/types.h>

#include <unordered_map>
#include <vector>

#include "base_view.h"
#include "src/lib/fxl/macros.h"

namespace frame_compression {

class SoftwareView : public BaseView {
 public:
  SoftwareView(scenic::ViewContext context, uint64_t modifier, uint32_t width, uint32_t height,
               uint32_t paint_count, FILE* png_fp, inspect::Node inspect_node);
  ~SoftwareView() override = default;

 private:
  struct Tile {
    bool operator==(const Tile& other) const {
      return memcmp(data, other.data, kTileNumBytes) == 0;
    }
    uint32_t* data;
  };

  class TileHashFunction {
   public:
    size_t operator()(const Tile& tile) const {
      size_t result = 0;
      for (size_t i = 0; i < kTileNumPixels; ++i) {
        result = (result * 31) ^ tile.data[i];
      }
      return result;
    }
  };

  struct Image {
    uint32_t image_id;
    uint8_t* vmo_ptr;
    size_t image_bytes;
    size_t image_bytes_used = 0;
    size_t image_bytes_deduped = 0;
    uint32_t stride = 0;
    uint32_t width_in_tiles = 0;
    uint32_t height_in_tiles = 0;
    bool needs_flush;
    std::unordered_map<Tile, uint32_t, TileHashFunction> tiles;
    inspect::LazyNode inspect_node;
  };

  // |scenic::BaseView|
  void OnSceneInvalidated(fuchsia::images::PresentationInfo presentation_info) override;

  void SetPixelsFromColorOffset(Image& image, uint32_t color_offset);
  void SetAfbcPixelsFromColorOffset(Image& image, uint32_t color_offset);
  void SetLinearPixelsFromColorOffset(Image& image, uint32_t color_offset);

  void SetPixelsFromPng(Image& image, png_structp png);
  void SetAfbcPixelsFromPng(Image& image, png_structp png);
  void SetLinearPixelsFromPng(Image& image, png_structp png);

  fit::promise<inspect::Inspector> PopulateStats() const;
  fit::promise<inspect::Inspector> PopulateImageStats(const Image& image) const;

  const uint64_t modifier_;
  const uint32_t paint_count_;
  FILE* const png_fp_;
  fuchsia::sysmem::AllocatorSyncPtr sysmem_allocator_;
  Image images_[kNumImages];
  std::vector<png_bytep> row_pointers_;
  std::vector<uint32_t> scratch_;
  inspect::LazyNode inspect_node_;

  FXL_DISALLOW_COPY_AND_ASSIGN(SoftwareView);
};

}  // namespace frame_compression

#endif  // SRC_UI_EXAMPLES_FRAME_COMPRESSION_SOFTWARE_VIEW_H_

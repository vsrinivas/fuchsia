// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base_view.h"

#include <lib/trace/event.h>
#include <lib/ui/scenic/cpp/commands.h>

#include <cmath>

namespace frame_compression {

namespace {

constexpr float kDisplayHeight = 50;
constexpr float kInitialWindowXPos = 320;
constexpr float kInitialWindowYPos = 240;

// Inspect values.
constexpr char kBaseView[] = "base_view";
constexpr char kWidth[] = "width";
constexpr char kHeight[] = "height";

}  // namespace

BaseView::BaseView(scenic::ViewContext context, const std::string& debug_name, uint32_t width,
                   uint32_t height, inspect::Node inspect_node)
    : scenic::BaseView(std::move(context), debug_name),
      width_(width),
      height_(height),
      material_(session()),
      top_inspect_node_(std::move(inspect_node)),
      next_color_offset_(height / 2),
      node_(session()),
      inspect_node_(
          top_inspect_node_.CreateLazyValues(kBaseView, [this] { return PopulateStats(); })) {
  // Create a rectangle shape to display on.
  scenic::Rectangle shape(session(), width_, height_);

  node_.SetShape(shape);
  node_.SetMaterial(material_);
  root_node().AddChild(node_);

  // Translation of 0, 0 is the middle of the screen
  node_.SetTranslation(kInitialWindowXPos, kInitialWindowYPos, -kDisplayHeight);
}

png_structp BaseView::CreatePngReadStruct(FILE* png_fp, png_infop* info_ptr_ptr) {
  fseek(png_fp, 0, SEEK_SET);
  uint8_t header[8];
  fread(header, 1, 8, png_fp);
  FX_CHECK(png_sig_cmp(header, 0, 8) == 0) << "File is not recognized as a PNG file";
  png_structp png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
  FX_CHECK(png_ptr) << "png_create_read_struct failed";
  if (setjmp(png_jmpbuf(png_ptr))) {
    FX_CHECK(false) << "error during png_init_io";
  }
  png_init_io(png_ptr, png_fp);
  png_set_sig_bytes(png_ptr, 8);
  png_infop info_ptr = png_create_info_struct(png_ptr);
  FX_CHECK(info_ptr) << "png_create_info_struct failed";
  png_read_info(png_ptr, info_ptr);
  FX_CHECK(png_get_interlace_type(png_ptr, info_ptr) == PNG_INTERLACE_NONE);
  png_byte color_type = png_get_color_type(png_ptr, info_ptr);
  FX_CHECK(color_type == PNG_COLOR_TYPE_RGB || color_type == PNG_COLOR_TYPE_RGB_ALPHA);
  if (color_type == PNG_COLOR_TYPE_RGB) {
    png_set_filler(png_ptr, 0xff, PNG_FILLER_AFTER);
  }
  png_byte bit_depth = png_get_bit_depth(png_ptr, info_ptr);
  if (bit_depth < 8) {
    png_set_packing(png_ptr);
  } else if (bit_depth == 16) {
    png_set_strip_16(png_ptr);
    png_set_swap(png_ptr);
  }
  *info_ptr_ptr = info_ptr;
  return png_ptr;
}

void BaseView::DestroyPngReadStruct(png_structp png_ptr, png_infop info_ptr) {
  png_destroy_read_struct(&png_ptr, &info_ptr, nullptr);
}

uint32_t BaseView::GetNextImageIndex() {
  const auto rv = next_image_index_;
  next_image_index_ = (next_image_index_ + 1) % kNumImages;
  return rv;
}

uint32_t BaseView::GetNextColorOffset() {
  const auto rv = next_color_offset_;
  next_color_offset_ = (next_color_offset_ + 1) % height_;
  return rv;
}

uint32_t BaseView::GetNextFrameNumber() {
  const auto rv = next_frame_number_;
  next_frame_number_ = next_frame_number_ + 1;
  return rv;
}

void BaseView::Animate(fuchsia::images::PresentationInfo presentation_info) {
  // Compute the amount of time that has elapsed since the view was created.
  double seconds = static_cast<double>(presentation_info.presentation_time) / 1'000'000'000;

  const float kHalfWidth = logical_size().x * 0.5f;
  const float kHalfHeight = logical_size().y * 0.5f;

  // Compute the translation for the window to swirl around the screen.
  node_.SetTranslation(kHalfWidth * (1. + .1 * sin(seconds * 0.8)),
                       kHalfHeight * (1. + .1 * sin(seconds * 0.6)), -kDisplayHeight);
}

fit::promise<inspect::Inspector> BaseView::PopulateStats() const {
  inspect::Inspector inspector;

  inspector.GetRoot().CreateUint(kWidth, width_, &inspector);
  inspector.GetRoot().CreateUint(kHeight, height_, &inspector);

  return fit::make_ok_promise(std::move(inspector));
}

}  // namespace frame_compression

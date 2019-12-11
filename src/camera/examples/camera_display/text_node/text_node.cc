// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fzl/vmo-mapper.h>
#include <lib/gfx-font-data/gfx-font-data.h>
#include <text_node.h>

#include <algorithm>

#include "src/lib/syslog/cpp/logger.h"

TextNode::TextNode(scenic::Session* session) : scenic::Node(session) {
  session->Enqueue(scenic::NewCreateShapeNodeCmd(id()));
}

TextNode::TextNode(TextNode&& moved) : scenic::Node(std::move(moved)) {}

TextNode::~TextNode() = default;

zx_status_t TextNode::SetText(const std::string s) {
  static constexpr const uint32_t kBitmapScale = 1;  // Per-dimension pixel scatter ratio.
  static constexpr const uint32_t kTextForeground = 0xFF000000;  // Opaque black.
  static constexpr const uint32_t kTextBackground = 0x7FFFFFFF;  // Semi-transparent white.
  static constexpr const struct {
    uint8_t r = 0xFF;
    uint8_t g = 0xFF;
    uint8_t b = 0xFF;
    uint8_t a = 0xFE;
  } kShapeColor;  // Nearly opaque white.

  const auto& font = gfx_font_9x16;
  static constexpr const uint32_t kFontDataBits = 8;

  fuchsia::images::ImageInfo image_info;
  uint32_t left_pad = font.width - kFontDataBits;
  image_info.width = (font.width * s.size() + left_pad) * kBitmapScale;
  image_info.height = font.height * kBitmapScale;
  image_info.stride = image_info.width * sizeof(uint32_t);
  image_info.alpha_format = fuchsia::images::AlphaFormat::NON_PREMULTIPLIED;
  size_t image_size = image_info.width * image_info.height * sizeof(uint32_t);

  zx::vmo vmo;
  fzl::VmoMapper mapper;
  zx_status_t status =
      mapper.CreateAndMap(image_size, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, nullptr, &vmo);
  if (status != ZX_OK) {
    FX_PLOGS(ERROR, status);
    return status;
  }

  auto bitmap_data = reinterpret_cast<uint32_t*>(mapper.start());

  size_t x_offset = left_pad * kBitmapScale;
  for (uint32_t v = 0; v < image_info.height; ++v) {
    for (uint32_t u = 0; u < x_offset; ++u) {
      bitmap_data[image_info.width * v + u] = kTextBackground;
    }
  }
  for (auto c : s) {
    if (c & 0x80) {  // Font only defines lower ASCII bitmaps.
      c = '?';
    }
    const auto* char_data = &font.data[c * font.height];
    for (uint32_t v = 0; v < font.height; ++v) {
      for (uint32_t u = 0; u < font.width; ++u) {
        uint32_t pixel = kTextBackground;
        if (u < kFontDataBits && (char_data[v] & (1u << u))) {
          pixel = kTextForeground;
        }
        for (uint32_t vv = 0; vv < kBitmapScale; ++vv) {
          for (uint32_t uu = 0; uu < kBitmapScale; ++uu) {
            bitmap_data[image_info.width * (v * kBitmapScale + vv) + u * kBitmapScale + uu +
                        x_offset] = pixel;
          }
        }
      }
    }
    x_offset += font.width * kBitmapScale;
  }

  mapper.Unmap();

  scenic::Memory memory(session(), std::move(vmo), image_size,
                        fuchsia::images::MemoryType::HOST_MEMORY);
  scenic::Image image(memory, 0, fidl::Clone(image_info));
  scenic::Material material(session());
  material.SetTexture(image);
  // TODO(fxb/38373): support is_alpha
  material.SetColor(kShapeColor.r, kShapeColor.g, kShapeColor.b, kShapeColor.a);

  scenic::Rectangle shape(session(), image_info.width * 1.0f / kBitmapScale,
                          image_info.height * 1.0f / kBitmapScale);
  session()->Enqueue(scenic::NewSetShapeCmd(id(), shape.id()));
  session()->Enqueue(scenic::NewSetMaterialCmd(id(), material.id()));

  return ZX_OK;
}

// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/camera/lib/image_writer/blc.h"

#include <array>

#include "src/camera/lib/image_writer/raw12_writer.h"

namespace camera {

BlcResult BlcRaw12(const zx::vmo* vmo, uint32_t width, uint32_t height, uint8_t bytes_per_pixel) {
  std::vector<uint8_t> buf(width * height * bytes_per_pixel);
  std::pair<uint16_t, uint16_t> pixel_pair;

  vmo->read(&buf.front(), 0, buf.size());

  std::array<uint8_t, kBytesPerDoublePixel> mega_pixel;
  uint32_t r = 0, gr = 0, gb = 0, b = 0;
  uint32_t num_r = 0, num_b = 0;

  for (uint32_t i = 0; i < buf.size(); i += bytes_per_pixel) {
    mega_pixel[0] = buf[i];
    mega_pixel[1] = buf[i + 1];
    mega_pixel[2] = buf[i + 2];
    pixel_pair = DoublePixelToPixelValues(mega_pixel);

    if ((i / width / bytes_per_pixel) % 2 == 0) {
      camera::AddValsFromPairToTargetInts(&r, &gr, pixel_pair);
      ++num_r;
    } else {
      camera::AddValsFromPairToTargetInts(&gb, &b, pixel_pair);
      ++num_b;
    }
  }

  uint32_t avg_r = r / num_r, avg_gr = gr / num_r, avg_gb = gb / num_b, avg_b = b / num_b;

  return BlcResult{avg_r, avg_gr, avg_gb, avg_b};
}

void AddValsFromPairToTargetInts(uint32_t* first_val, uint32_t* second_val,
                                 std::pair<uint16_t, uint16_t> p) {
  *first_val += p.first;
  *second_val += p.second;
}

}  // namespace camera

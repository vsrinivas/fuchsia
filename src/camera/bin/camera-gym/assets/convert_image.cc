// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include <fstream>
#include <iostream>

// Converts from RGBA straight alpha into BGRA premultiplied alpha.
int main(int argc, char* argv[]) {
  if (argc != 3) {
    std::cerr << "Usage: " << argv[0] << " INPUT OUTPUT" << std::endl;
    return EXIT_FAILURE;
  }
  std::ifstream src(argv[1], std::ios::in | std::ios::binary);
  if (!src.is_open()) {
    std::cerr << "Failed to open " << argv[1] << " for reading." << std::endl;
    return EXIT_FAILURE;
  }
  std::ofstream dest(argv[2], std::ios::out | std::ios::binary);
  if (!dest.is_open()) {
    std::cerr << "Failed to open " << argv[2] << " for writing." << std::endl;
    return EXIT_FAILURE;
  }
  char pixel[4]{};
  while (src.read(pixel, sizeof(pixel))) {
    uint32_t r = static_cast<uint8_t>(pixel[0]);
    uint32_t g = static_cast<uint8_t>(pixel[1]);
    uint32_t b = static_cast<uint8_t>(pixel[2]);
    uint32_t a = static_cast<uint8_t>(pixel[3]);
    pixel[0] = static_cast<char>((b * a + 127) / 255);
    pixel[1] = static_cast<char>((g * a + 127) / 255);
    pixel[2] = static_cast<char>((r * a + 127) / 255);
    if (dest.write(pixel, sizeof(pixel)).bad()) {
      std::cerr << "Failed writing data to " << argv[2] << "." << std::endl;
      return EXIT_FAILURE;
    }
  }
  return EXIT_SUCCESS;
}

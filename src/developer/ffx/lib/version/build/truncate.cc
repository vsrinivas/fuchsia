// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <filesystem>
#include <fstream>
#include <iostream>

// Copies the input file to an output file of a specific size, zero padded.
int main(int argc, char* argv[]) {
  if (argc < 4) {
    std::cerr << "Usage: " << argv[0] << " output-file input-file size" << std::endl;
    return 1;
  }

  const auto input_path = argv[1];
  const auto output_path = argv[2];
  const size_t size = atoi(argv[3]);

  const auto input_size = std::filesystem::file_size(input_path);

  std::string input_buf(size, '\0');
  std::ifstream inputs(input_path, std::ios::binary);
  if (!inputs) {
    std::cerr << strerror(errno) << std::endl;
    return 1;
  }
  inputs.read(input_buf.data(), std::min(input_size, size));
  if (!inputs) {
    std::cerr << strerror(errno) << std::endl;
    return 1;
  }

  if (std::filesystem::exists(output_path)) {
    const auto output_size = std::filesystem::file_size(output_path);
    if (output_size == size) {
      std::string output_buf(size, '\0');
      auto outputs = std::ifstream(output_path, std::ios::binary);
      if (!outputs) {
        std::cerr << strerror(errno) << std::endl;
        return 1;
      }
      outputs.read(output_buf.data(), size);
      if (!outputs) {
        std::cerr << strerror(errno) << std::endl;
        return 1;
      }

      if (input_buf == output_buf) {
        return 0;
      }
    }
  }

  std::ofstream outputs(output_path, std::ios::binary | std::ios::trunc);
  if (!outputs) {
    std::cerr << strerror(errno) << std::endl;
    return 1;
  }
  outputs.write(input_buf.c_str(), input_buf.size());
  if (!outputs) {
    std::cerr << strerror(errno) << std::endl;
    return 1;
  }
  outputs.close();
  if (!outputs) {
    std::cerr << strerror(errno) << std::endl;
    return 1;
  }
  return 0;
}

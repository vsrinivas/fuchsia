// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/extractor/bin/parse.h"

#include <fcntl.h>
#include <getopt.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <zircon/errors.h>

#include <iostream>

namespace extractor {

// Prints usage message for the utility.
void PrintUsage() {
  std::cerr << "usage: disk-extract [ <option>* ] --type [disk-type] --disk [disk-path] --image "
               "[image-path]\n";
  std::cerr << "Extracts disk image from disk-path and writes the image to [image-path]\n";
  std::cerr << "where disk-path contains disk-type image.\n";
  std::cerr << "  --type : \"minfs\" is the only allowed type.\n";
  std::cerr << "  --disk: Path of the device file that needs to be extracted.\n";
  std::cerr << "  --image: Path of the image file where extracted image will be written to.\n";
  std::cerr << "  --dump-pii : dumps pii in addition to disk metadata.\n";
  std::cerr << "  --help : Show this help message\n";
}

zx::status<ExtractOptions> ParseCommandLineArguments(int argc, char* const argv[]) {
  ExtractOptions options;
  static const struct option opts[] = {
      {"disk", required_argument, nullptr, 'd'}, {"image", required_argument, nullptr, 'i'},
      {"type", required_argument, nullptr, 't'}, {"dump-pii", no_argument, nullptr, 'p'},
      {"help", no_argument, nullptr, 'h'},       {nullptr, 0, nullptr, 0},
  };
  for (int opt; (opt = getopt_long(argc, argv, "d:i:t:p:h", opts, nullptr)) != -1;) {
    switch (opt) {
      case 'd':
        options.input_path.assign(optarg);
        if (options.input_path.length() == 0) {
          std::cerr << "Missing disk path argument" << std::endl;
          PrintUsage();
          return zx::error(ZX_ERR_INVALID_ARGS);
        }
        options.input_fd.reset(open(options.input_path.c_str(), O_RDONLY));
        if (!options.input_fd) {
          std::cerr << "Failed to open input path " << options.input_path << std::endl;
          return zx::error(ZX_ERR_IO);
        }
        break;
      case 'i':
        options.output_path.assign(optarg);
        if (options.output_path.length() == 0) {
          std::cerr << "Missing image path argument" << options.output_path << std::endl;
          PrintUsage();
          return zx::error(ZX_ERR_INVALID_ARGS);
        }

        struct stat stats;
        if (stat(options.output_path.c_str(), &stats) == 0) {
          std::cerr << "Image file already exists" << options.output_path << std::endl;
          return zx::error(ZX_ERR_ALREADY_EXISTS);
        }
        options.output_fd.reset(open(options.output_path.c_str(), O_RDWR | O_CREAT));
        if (!options.output_fd) {
          std::cerr << "Failed to open/create image file" << options.output_path << std::endl;
          return zx::error(ZX_ERR_IO);
        }
        break;
      case 't':
        if (strncmp(optarg, "minfs", strlen(optarg)) != 0) {
          std::cerr << "Type supplied " << optarg << " and needs to be minfs\n";
          return zx::error(ZX_ERR_INVALID_ARGS);
        }
        options.type = DiskType::kMinfs;
        break;
      case 'p':
        std::cerr << "Dumping Pii\n";
        options.dump_pii = true;
        break;
      case 'h':
        __FALLTHROUGH;
      default:
        PrintUsage();
        return zx::error(ZX_ERR_INVALID_ARGS);
    }
  }

  if (options.type == std::nullopt || !options.output_fd || !options.input_fd) {
    PrintUsage();
    return zx::error(ZX_ERR_INVALID_ARGS);
  }

  return zx::ok(std::move(options));
}

}  // namespace extractor

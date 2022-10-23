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

#include "lib/zx/result.h"

namespace extractor {
namespace {

// Prints usage message for the utility.
void PrintUsage() {
  std::cerr
      << "usage:\n"
      << "disk-extract extract [ <option>* ] --type [disk-type] --disk [disk-path]"
      << " --image [image-path]\n"
      << "  Extracts disk image from disk-path and writes the image to [image-path]\n"
      << "  where disk-path contains disk-type image.\n"
      << "    --type : \"minfs\" is the only allowed type.\n"
      << "    --disk: Path of the device file that needs to be extracted.\n"
      << "    --image: Path of the image file where extracted image will be written to.\n"
      << "    --dump-pii : dumps pii in addition to disk metadata.\n"
      << "disk-extract deflate [--verbose] --input-file <input-image> "
      << "--output-file <output-file>\n"
      << "  Deflates an extracted disk image into its original form\n"
      << "    --input-file: The path of the extracted image file.\n"
      << "    --output-file: The path where deflated file will be created.\n"
      << "    --verbose: Prints additional info about extracted imagewhile deflating the file.\n"
      << "--help : Show this help message\n";
}

zx::result<> ParseInputFile(const char* path, ExtractOptions& options) {
  options.input_path.assign(path);
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
  return zx::ok();
}

zx::result<> ParseOutputFile(const char* path, ExtractOptions& options) {
  options.output_path.assign(path);
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
  options.output_fd.reset(open(options.output_path.c_str(), O_RDWR | O_CREAT, S_IRUSR | S_IWUSR));
  if (!options.output_fd) {
    std::cerr << "Failed to open/create image file" << options.output_path << std::endl;
    return zx::error(ZX_ERR_IO);
  }
  return zx::ok();
}

}  // namespace

zx::result<ExtractOptions> ParseExtractArguments(int argc, char* const argv[]) {
  ExtractOptions options;
  options.sub_command = SubCommand::kExtract;

  static const struct option opts[] = {
      {"disk", required_argument, nullptr, 'd'}, {"image", required_argument, nullptr, 'i'},
      {"type", required_argument, nullptr, 't'}, {"dump-pii", no_argument, nullptr, 'p'},
      {"help", no_argument, nullptr, 'h'},       {nullptr, 0, nullptr, 0},
  };
  for (int opt; (opt = getopt_long(argc, argv, "d:i:t:p:h", opts, nullptr)) != -1;) {
    switch (opt) {
      case 'd':
        if (auto status = ParseInputFile(optarg, options); status.is_error()) {
          return zx::error(status.error_value());
        }
        break;

      case 'i':
        if (auto status = ParseOutputFile(optarg, options); status.is_error()) {
          return zx::error(status.error_value());
        }
        break;
      case 't':
        if (strncmp(optarg, "minfs", strlen(optarg)) == 0) {
          options.type = DiskType::kMinfs;
        } else if (strncmp(optarg, "blobfs", strlen(optarg)) == 0) {
          options.type = DiskType::kBlobfs;
        } else if (strncmp(optarg, "fvm", strlen(optarg)) == 0) {
          options.type = DiskType::kFvm;
        } else {
          std::cerr << "Type supplied " << optarg
                    << " and needs to be either minfs, blobfs, or fvm\n";
          return zx::error(ZX_ERR_INVALID_ARGS);
        }
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

zx::result<ExtractOptions> ParseDeflateArguments(int argc, char* const argv[]) {
  ExtractOptions options;
  options.sub_command = SubCommand::kDeflate;

  static const struct option opts[] = {
      {"input_file", required_argument, nullptr, 'i'},
      {"output_file", required_argument, nullptr, 'o'},
      {"verbose", no_argument, nullptr, 'v'},
      {"help", no_argument, nullptr, 'h'},
      {nullptr, 0, nullptr, 0},
  };
  for (int opt; (opt = getopt_long(argc, argv, "i:o:v:h", opts, nullptr)) != -1;) {
    switch (opt) {
      case 'i':
        if (auto status = ParseInputFile(optarg, options); status.is_error()) {
          return zx::error(status.error_value());
        }
        break;

      case 'o':
        if (auto status = ParseOutputFile(optarg, options); status.is_error()) {
          return zx::error(status.error_value());
        }
        break;
      case 'v':
        options.verbose = true;
        break;
      case 'h':
        __FALLTHROUGH;
      default:
        PrintUsage();
        return zx::error(ZX_ERR_INVALID_ARGS);
    }
  }

  if (!options.output_fd || !options.input_fd) {
    PrintUsage();
    return zx::error(ZX_ERR_INVALID_ARGS);
  }

  return zx::ok(std::move(options));
}

zx::result<ExtractOptions> ParseCommandLineArguments(int argc, char* const argv[]) {
  if (argc <= 1) {
    PrintUsage();
    return zx::error(ZX_ERR_INVALID_ARGS);
  }
  if (strcmp(argv[1], "extract") == 0) {
    return ParseExtractArguments(argc - 1, &argv[1]);
  }
  if (strcmp(argv[1], "deflate") == 0) {
    return ParseDeflateArguments(argc - 1, &argv[1]);
  }
  std::cerr << "subcommand missing." << std::endl;
  PrintUsage();
  return zx::error(ZX_ERR_INVALID_ARGS);
}

}  // namespace extractor

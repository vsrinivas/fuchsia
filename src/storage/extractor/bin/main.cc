// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstdlib>
#include <iostream>

#include "src/storage/extractor/bin/parse.h"
#include "src/storage/extractor/c/extractor.h"
#include "src/storage/extractor/cpp/extractor.h"
#include "src/storage/minfs/format.h"

int Extract(extractor::ExtractOptions& args) {
  ExtractorOptions options = ExtractorOptions{
      .force_dump_pii = args.dump_pii, .add_checksum = false, .alignment = minfs::kMinfsBlockSize};
  auto extractor_or =
      extractor::Extractor::Create(args.input_fd.duplicate(), options, args.output_fd.duplicate());
  if (extractor_or.is_error()) {
    std::cerr << "Failed to create extractor" << std::endl;
    return EXIT_FAILURE;
  }
  auto extractor = std::move(extractor_or.value());
  zx::result<> status;
  if (!args.type.has_value()) {
    std::cerr << "extract options type is none, error:  " << status.error_value() << std::endl;
    return EXIT_FAILURE;
  }
  switch (args.type.value()) {
    case extractor::DiskType::kMinfs:
      status = extractor::MinfsExtract(std::move(args.input_fd), *extractor);
      if (status.is_error()) {
        std::cerr << "minfs extraction failed with " << status.error_value() << std::endl;
        return EXIT_FAILURE;
      }
      break;
    case extractor::DiskType::kBlobfs:
      status = extractor::BlobfsExtract(std::move(args.input_fd), *extractor);
      if (status.is_error()) {
        std::cerr << "blobfs extraction failed with " << status.error_value() << std::endl;
        return EXIT_FAILURE;
      }
      break;
    case extractor::DiskType::kFvm:
      status = extractor::FvmExtract(std::move(args.input_fd), *extractor);
      if (status.is_error()) {
        std::cerr << "fvm extraction failed with " << status.error_value() << std::endl;
        return EXIT_FAILURE;
      }
  }
  status = extractor->Write();
  if (status.is_error()) {
    std::cerr << "Failed to write extracted image " << status.error_value() << std::endl;
    return EXIT_FAILURE;
  }

  return 0;
}

int Deflate(extractor::ExtractOptions& args) {
  fbl::unique_fd verbose_stream;
  if (args.verbose) {
    verbose_stream.reset(dup(fileno(stdout)));
    if (!verbose_stream.is_valid()) {
      std::cerr << "Failed to open stdout for verbose stream" << std::endl;
      return EXIT_FAILURE;
    }
  }
  auto status = extractor::Extractor::Deflate(args.input_fd.duplicate(), args.output_fd.duplicate(),
                                              std::move(verbose_stream));
  if (status.is_error()) {
    std::cerr << "Deflate failed with " << status.status_value() << std::endl;
    return EXIT_FAILURE;
  }
  return 0;
}

int main(int argc, char** argv) {
  auto args_or = extractor::ParseCommandLineArguments(argc, argv);

  if (args_or.is_error()) {
    return -1;
  }
  auto args = std::move(args_or.value());

  if (args.sub_command == extractor::SubCommand::kExtract) {
    return Extract(args);
  }
  return Deflate(args);
}

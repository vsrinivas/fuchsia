// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string.h>

#include <fstream>
#include <memory>
#include <string>

#include <fbl/unique_fd.h>

#include "src/storage/blobfs/host.h"
#include "src/storage/blobfs/tools/blobfs_creator.h"

int ExportBlobs(std::string& source_path, std::string& output_path) {
  fbl::unique_fd blobfs_image(open(source_path.c_str(), O_RDONLY));
  if (!blobfs_image.is_valid()) {
    fprintf(stderr, "Failed to open blobfs image at %s. More specifically: %s.\n",
            source_path.c_str(), strerror(errno));
    return -1;
  }

  std::unique_ptr<blobfs::Blobfs> fs = nullptr;
  if (blobfs::blobfs_create(&fs, std::move(blobfs_image)) != ZX_OK) {
    return -1;
  }

  // Try to create path if it doesn't exist.
  std::filesystem::create_directories(output_path);
  fbl::unique_fd output_fd(open(output_path.c_str(), O_DIRECTORY));
  if (!output_fd.is_valid()) {
    fprintf(stderr, "Failed to obtain a handle to output path at %s. More specifically: %s.\n",
            output_path.c_str(), strerror(errno));
    return -1;
  }

  auto export_result = blobfs::ExportBlobs(output_fd.get(), *fs);
  if (export_result.is_error()) {
    fprintf(stderr, "Encountered error while exporting blobs. More specifically: %s.\n",
            export_result.error().c_str());
    return -1;
  }
  fprintf(stderr, "Successfully exported all blobs.\n");
  return 0;
}

int main(int argc, char** argv) {
  BlobfsCreator blobfs;

  if (argc > 3) {
    if (strcmp(argv[1], "export") == 0) {
      std::string image_path = argv[2];
      std::string output_path = argv[3];
      return ExportBlobs(image_path, output_path);
    }
  }

  if (blobfs.ProcessAndRun(argc, argv) != ZX_OK) {
    return -1;
  }

  return 0;
}

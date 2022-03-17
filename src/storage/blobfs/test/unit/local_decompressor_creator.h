
// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_BLOBFS_TEST_UNIT_LOCAL_DECOMPRESSOR_CREATOR_H_
#define SRC_STORAGE_BLOBFS_TEST_UNIT_LOCAL_DECOMPRESSOR_CREATOR_H_

#include <fuchsia/blobfs/internal/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fidl/cpp/binding.h>

#include <memory>
#include <vector>

#include "src/storage/blobfs/compression/decompressor_sandbox/decompressor_impl.h"
#include "src/storage/blobfs/compression/external_decompressor.h"

namespace blobfs {

class LocalDecompressorCreator {
 public:
  // Disallow copy.
  LocalDecompressorCreator(const LocalDecompressorCreator&) = delete;

  static zx::status<std::unique_ptr<LocalDecompressorCreator>> Create();

  DecompressorCreatorConnector* GetDecompressorConnector() { return connector_.get(); }

 private:
  LocalDecompressorCreator() : loop_(&kAsyncLoopConfigNoAttachToCurrentThread) {}

  zx_status_t RegisterChannel(zx::channel);

  blobfs::DecompressorImpl decompressor_;
  async::Loop loop_;
  std::mutex bindings_lock_;
  // This is wrapped in unique_ptr because it is not movable, as is required for vector erase.
  std::vector<std::unique_ptr<fidl::Binding<fuchsia::blobfs::internal::DecompressorCreator>>>
      bindings_;
  std::unique_ptr<DecompressorCreatorConnector> connector_;
};

}  // namespace blobfs

#endif  // SRC_STORAGE_BLOBFS_TEST_UNIT_LOCAL_DECOMPRESSOR_CREATOR_H_

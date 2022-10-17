
// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_BLOBFS_TEST_UNIT_LOCAL_DECOMPRESSOR_CREATOR_H_
#define SRC_STORAGE_BLOBFS_TEST_UNIT_LOCAL_DECOMPRESSOR_CREATOR_H_

#include <fuchsia/blobfs/internal/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fidl/cpp/binding.h>

#include <list>
#include <memory>

#include "src/storage/blobfs/compression/decompressor_sandbox/decompressor_impl.h"
#include "src/storage/blobfs/compression/external_decompressor.h"

namespace blobfs {

class LocalDecompressorCreator {
 public:
  // Disallow copy.
  LocalDecompressorCreator(const LocalDecompressorCreator&) = delete;

  ~LocalDecompressorCreator();

  static zx::result<std::unique_ptr<LocalDecompressorCreator>> Create();

  DecompressorCreatorConnector& GetDecompressorConnector() { return *connector_; }

 private:
  LocalDecompressorCreator() : loop_(&kAsyncLoopConfigNoAttachToCurrentThread) {}

  // Called on the server thread. Removes dead channels then binds the new one.
  zx_status_t RegisterChannelOnServerThread(zx::channel);

  // Removes dead channels then binds the given channel to the local server.
  zx_status_t RegisterChannel(zx::channel);

  blobfs::DecompressorImpl decompressor_;
  async::Loop loop_;
  std::unique_ptr<DecompressorCreatorConnector> connector_;
  // Track existing bindings. Only accessed from the server thread.
  std::list<fidl::Binding<fuchsia::blobfs::internal::DecompressorCreator>> bindings_;
  // Used to prevent new connections during teardown. Only accessed from the server thread.
  bool shutting_down_ = false;
};

}  // namespace blobfs

#endif  // SRC_STORAGE_BLOBFS_TEST_UNIT_LOCAL_DECOMPRESSOR_CREATOR_H_

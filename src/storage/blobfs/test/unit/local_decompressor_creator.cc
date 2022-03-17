// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "local_decompressor_creator.h"

#include <lib/sys/cpp/component_context.h>
#include <lib/zx/channel.h>

#include "lib/zx/status.h"
#include "src/storage/blobfs/compression/external_decompressor.h"

namespace blobfs {

class LambdaConnector : public DecompressorCreatorConnector {
 public:
  explicit LambdaConnector(std::function<zx_status_t(zx::channel)> callback);

  // ExternalDecompressorCreatorConnector interface.
  zx_status_t ConnectToDecompressorCreator(zx::channel remote_channel) override;

 private:
  std::function<zx_status_t(zx::channel)> callback_;
};

LambdaConnector::LambdaConnector(std::function<zx_status_t(zx::channel)> callback)
    : callback_(std::move(callback)) {}

zx_status_t LambdaConnector::ConnectToDecompressorCreator(zx::channel remote_channel) {
  return callback_(std::move(remote_channel));
}

zx::status<std::unique_ptr<LocalDecompressorCreator>> LocalDecompressorCreator::Create() {
  std::unique_ptr<LocalDecompressorCreator> decompressor(new LocalDecompressorCreator());
  decompressor->connector_ = std::make_unique<LambdaConnector>(
      [decompressor = decompressor.get()](zx::channel remote_channel) {
        return decompressor->RegisterChannel(std::move(remote_channel));
      });
  if (zx_status_t status = decompressor->loop_.StartThread(); status != ZX_OK) {
    return zx::error(status);
  }
  return zx::ok(std::move(decompressor));
}

zx_status_t LocalDecompressorCreator::RegisterChannel(zx::channel channel) {
  // First remove existing dead bindings.
  std::lock_guard l(bindings_lock_);
  auto it = bindings_.begin();
  while (it != bindings_.end()) {
    if (!it->get()->is_bound()) {
      bindings_.erase(it);
    } else {
      ++it;
    }
  }
  // Add new binding.
  bindings_.emplace_back(
      new fidl::Binding<fuchsia::blobfs::internal::DecompressorCreator>(&decompressor_));
  return bindings_.back()->Bind(std::move(channel), loop_.dispatcher());
}

}  // namespace blobfs

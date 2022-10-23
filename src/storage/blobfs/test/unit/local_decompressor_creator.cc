// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "local_decompressor_creator.h"

#include <lib/async/cpp/task.h>
#include <lib/sync/completion.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/zx/channel.h>
#include <lib/zx/result.h>
#include <zircon/errors.h>
#include <zircon/time.h>

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

zx::result<std::unique_ptr<LocalDecompressorCreator>> LocalDecompressorCreator::Create() {
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

LocalDecompressorCreator::~LocalDecompressorCreator() {
  sync_completion_t done;
  // Unbind everything from the server thread and prevent future bindings.
  ZX_ASSERT(ZX_OK == async::PostTask(loop_.dispatcher(), [this, &done]() {
              this->shutting_down_ = true;
              for (auto& binding : bindings_) {
                binding.Close(ZX_ERR_CANCELED);
              }
              this->bindings_.clear();
              sync_completion_signal(&done);
            }));

  sync_completion_wait(&done, ZX_TIME_INFINITE);
}

zx_status_t LocalDecompressorCreator::RegisterChannel(zx::channel channel) {
  // Pushing binding management onto the server thread since the HLCPP bindings
  // are not thread safe.
  zx_status_t bind_status = ZX_OK;
  sync_completion_t done;
  zx_status_t post_status = async::PostTask(loop_.dispatcher(), [&]() mutable {
    if (this->shutting_down_) {
      bind_status = ZX_ERR_CANCELED;
    } else {
      bind_status = this->RegisterChannelOnServerThread(std::move(channel));
    }
    sync_completion_signal(&done);
  });
  if (post_status != ZX_OK) {
    return post_status;
  }

  sync_completion_wait(&done, ZX_TIME_INFINITE);
  return bind_status;
}

zx_status_t LocalDecompressorCreator::RegisterChannelOnServerThread(zx::channel channel) {
  auto it = bindings_.begin();
  while (it != bindings_.end()) {
    if (!it->is_bound()) {
      it = bindings_.erase(it);
    } else {
      ++it;
    }
  }
  // Add new binding.
  bindings_.emplace_back(&decompressor_);
  return bindings_.back().Bind(std::move(channel), loop_.dispatcher());
}

}  // namespace blobfs

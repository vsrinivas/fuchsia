// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/fuzzing/common/testing/corpus-reader.h"

#include <lib/syslog/cpp/macros.h>
#include <zircon/status.h>

#include "src/sys/fuzzing/common/async-socket.h"

namespace fuzzing {

FakeCorpusReader::FakeCorpusReader(ExecutorPtr executor)
    : binding_(this), executor_(std::move(executor)) {}

void FakeCorpusReader::Bind(fidl::InterfaceRequest<CorpusReader> request) {
  auto status = binding_.Bind(std::move(request), executor_->dispatcher());
  FX_DCHECK(status == ZX_OK) << zx_status_get_string(status);
}

fidl::InterfaceHandle<CorpusReader> FakeCorpusReader::NewBinding() {
  fidl::InterfaceHandle<CorpusReader> client;
  Bind(client.NewRequest());
  return client;
}

void FakeCorpusReader::Next(FidlInput fidl_input, NextCallback callback) {
  if (error_after_-- == 0) {
    callback(ZX_ERR_INTERNAL);
    return;
  }
  auto next = AsyncSocketRead(executor_, std::move(fidl_input))
                  .and_then([this](Input& input) {
                    corpus_.emplace_back(std::move(input));
                    return fpromise::ok();
                  })
                  .then([callback = std::move(callback)](ZxResult<>& result) mutable {
                    if (result.is_error()) {
                      callback(result.error());
                    } else {
                      callback(ZX_OK);
                    }
                    return fpromise::ok();
                  })
                  .wrap_with(scope_);
  executor_->schedule_task(std::move(next));
}

}  // namespace fuzzing

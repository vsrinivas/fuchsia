// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/fuzzing/common/testing/corpus-reader.h"

#include <lib/syslog/cpp/macros.h>
#include <zircon/status.h>

namespace fuzzing {

FakeCorpusReader::FakeCorpusReader() : binding_(this) {
  transceiver_ = std::make_shared<Transceiver>();
};

fidl::InterfaceHandle<CorpusReader> FakeCorpusReader::NewBinding(async_dispatcher_t* dispatcher) {
  binding_.set_dispatcher(dispatcher);
  binding_.set_error_handler([this](zx_status_t status) {
    FX_LOGS(WARNING) << zx_status_get_string(status);
    std::lock_guard<std::mutex> lock(mutex_);
    closed_ = true;
    sync_completion_signal(&sync_);
  });
  return binding_.NewBinding();
}

void FakeCorpusReader::Next(FidlInput fidl_input, NextCallback callback) {
  transceiver_->Receive(std::move(fidl_input), [&](zx_status_t status, Input input) {
    FX_DCHECK(status == ZX_OK) << zx_status_get_string(status);
    std::lock_guard<std::mutex> lock(mutex_);
    inputs_.push_back(std::move(input));
    sync_completion_signal(&sync_);
  });
  callback(ZX_OK);
}

bool FakeCorpusReader::AwaitNext() {
  while (true) {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (!inputs_.empty()) {
        return true;
      }
      if (closed_) {
        return false;
      }
    }
    sync_completion_wait(&sync_, ZX_TIME_INFINITE);
  }
}

Input FakeCorpusReader::GetNext() {
  Input input;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    FX_DCHECK(!inputs_.empty());
    input = std::move(inputs_.front());
    inputs_.pop_front();
    sync_completion_reset(&sync_);
  }
  return input;
}

}  // namespace fuzzing

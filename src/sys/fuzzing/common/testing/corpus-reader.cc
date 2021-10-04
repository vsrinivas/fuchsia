// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/fuzzing/common/testing/corpus-reader.h"

#include <lib/syslog/cpp/macros.h>
#include <zircon/status.h>

namespace fuzzing {

FakeCorpusReader::FakeCorpusReader(std::shared_ptr<Dispatcher> dispatcher)
    : binding_(this, std::move(dispatcher)) {}

fidl::InterfaceHandle<CorpusReader> FakeCorpusReader::NewBinding() { return binding_.NewBinding(); }

void FakeCorpusReader::Next(FidlInput fidl_input, NextCallback callback) {
  transceiver_.Receive(std::move(fidl_input), [&](zx_status_t status, Input input) {
    FX_DCHECK(status == ZX_OK) << zx_status_get_string(status);
    std::lock_guard<std::mutex> lock(mutex_);
    if (input.size() == 0) {
      has_more_ = false;
    } else {
      inputs_.push_back(std::move(input));
    }
    sync_completion_signal(&sync_);
  });
  callback(ZX_OK);
}

bool FakeCorpusReader::AwaitNext() {
  while (true) {
    sync_completion_wait(&sync_, ZX_TIME_INFINITE);
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (!inputs_.empty()) {
        return true;
      }
      if (!has_more_) {
        return false;
      }
    }
  }
}

Input FakeCorpusReader::GetNext() {
  Input input;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    FX_DCHECK(!inputs_.empty());
    input = std::move(inputs_.front());
    inputs_.pop_front();
    if (inputs_.empty() && has_more_) {
      sync_completion_reset(&sync_);
    }
  }
  return input;
}

}  // namespace fuzzing

// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/client/symbol_server.h"

#include "src/developer/debug/zxdb/client/cloud_storage_symbol_server.h"
#include "src/developer/debug/zxdb/common/string_util.h"

namespace zxdb {
namespace {

constexpr size_t kMaxRetries = 5;

}  // namespace

void SymbolServer::IncrementRetries() {
  if (++retries_ == kMaxRetries) {
    ChangeState(SymbolServer::State::kUnreachable);
  }
}

void SymbolServer::ChangeState(SymbolServer::State state) {
  if (state_ == state) {
    return;
  }

  state_ = state;

  if (state_ == SymbolServer::State::kReady) {
    retries_ = 0;
    error_log_.clear();
    ready_count_++;
  }

  if (state_change_callback_)
    state_change_callback_(this, state_);
}

std::unique_ptr<SymbolServer> SymbolServer::FromURL(Session* session,
                                                    const std::string& url) {
  if (StringBeginsWith(url, "gs://")) {
    return CloudStorageSymbolServer::Impl(session, url);
  }

  return nullptr;
}

}  // namespace zxdb

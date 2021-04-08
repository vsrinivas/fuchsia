// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BIN_DRIVER_MANAGER_FAKE_DRIVER_INDEX_H_
#define SRC_DEVICES_BIN_DRIVER_MANAGER_FAKE_DRIVER_INDEX_H_

#include <fuchsia/driver/framework/llcpp/fidl.h>
#include <lib/fit/function.h>
#include <lib/zx/status.h>

class FakeDriverIndex final : public fidl::WireInterface<fuchsia_driver_framework::DriverIndex> {
 public:
  struct MatchResult {
    std::string url;
    fuchsia_driver_framework::wire::NodeAddArgs matched_args;
  };

  using MatchCallback =
      fit::function<zx::status<MatchResult>(fuchsia_driver_framework::wire::NodeAddArgs args)>;

  FakeDriverIndex(async_dispatcher_t* dispatcher, MatchCallback match_callback)
      : dispatcher_(dispatcher), match_callback_(std::move(match_callback)) {}

  zx::status<fidl::ClientEnd<fuchsia_driver_framework::DriverIndex>> Connect() {
    auto endpoints = fidl::CreateEndpoints<fuchsia_driver_framework::DriverIndex>();
    if (endpoints.is_error()) {
      return zx::error(endpoints.status_value());
    }
    fidl::BindServer(dispatcher_, std::move(endpoints->server), this);
    return zx::ok(std::move(endpoints->client));
  }

  void MatchDriver(fuchsia_driver_framework::wire::NodeAddArgs args,
                   MatchDriverCompleter::Sync& completer) override {
    auto match = match_callback_(std::move(args));
    if (match.status_value() != ZX_OK) {
      completer.ReplyError(match.status_value());
      return;
    }
    completer.ReplySuccess(fidl::StringView::FromExternal(match.value().url),
                           std::move(match.value().matched_args));
  }

 private:
  async_dispatcher_t* dispatcher_;
  MatchCallback match_callback_;
};

#endif  // SRC_DEVICES_BIN_DRIVER_MANAGER_FAKE_DRIVER_INDEX_H_

// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BIN_DRIVER_MANAGER_FAKE_DRIVER_INDEX_H_
#define SRC_DEVICES_BIN_DRIVER_MANAGER_FAKE_DRIVER_INDEX_H_

#include <fuchsia/driver/framework/llcpp/fidl.h>
#include <lib/fit/function.h>
#include <lib/zx/status.h>

class FakeDriverIndex final : public fidl::WireServer<fuchsia_driver_framework::DriverIndex> {
 public:
  struct MatchResult {
    std::string url;
    std::optional<uint32_t> node_index;
    std::optional<uint32_t> num_nodes;
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

  void MatchDriver(MatchDriverRequestView request, MatchDriverCompleter::Sync& completer) override {
    auto match = match_callback_(request->args);
    if (match.status_value() != ZX_OK) {
      completer.ReplyError(match.status_value());
      return;
    }
    fidl::FidlAllocator allocator;
    fuchsia_driver_framework::wire::MatchedDriver driver(allocator);
    driver.set_url(allocator, fidl::StringView::FromExternal(match->url));
    if (match->node_index) {
      driver.set_node_index(allocator, *match->node_index);
    }
    if (match->num_nodes) {
      driver.set_num_nodes(allocator, *match->num_nodes);
    }
    completer.ReplySuccess(driver);
  }

 private:
  async_dispatcher_t* dispatcher_;
  MatchCallback match_callback_;
};

#endif  // SRC_DEVICES_BIN_DRIVER_MANAGER_FAKE_DRIVER_INDEX_H_

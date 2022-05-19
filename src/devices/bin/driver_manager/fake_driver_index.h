// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BIN_DRIVER_MANAGER_FAKE_DRIVER_INDEX_H_
#define SRC_DEVICES_BIN_DRIVER_MANAGER_FAKE_DRIVER_INDEX_H_

#include <fidl/fuchsia.driver.framework/cpp/wire.h>
#include <fidl/fuchsia.driver.index/cpp/wire.h>
#include <lib/fit/function.h>
#include <lib/zx/status.h>
#include <zircon/errors.h>

class FakeDriverIndex final : public fidl::WireServer<fuchsia_driver_index::DriverIndex> {
 public:
  struct CompositeDriverInfo {
    uint32_t node_index;
    uint32_t num_nodes;
    std::vector<std::string> node_names;
  };

  struct MatchResult {
    std::string url;
    std::optional<CompositeDriverInfo> composite;
    bool is_fallback = false;
  };

  using MatchCallback =
      fit::function<zx::status<MatchResult>(fuchsia_driver_framework::wire::NodeAddArgs args)>;

  FakeDriverIndex(async_dispatcher_t* dispatcher, MatchCallback match_callback)
      : dispatcher_(dispatcher), match_callback_(std::move(match_callback)) {}

  zx::status<fidl::ClientEnd<fuchsia_driver_index::DriverIndex>> Connect() {
    auto endpoints = fidl::CreateEndpoints<fuchsia_driver_index::DriverIndex>();
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

    fidl::Arena arena;
    completer.ReplySuccess(GetMatchedDriver(arena, match.value()));
  }

  void WaitForBaseDrivers(WaitForBaseDriversRequestView request,
                          WaitForBaseDriversCompleter::Sync& completer) override {
    completer.Reply();
  }

  void MatchDriversV1(MatchDriversV1RequestView request,
                      MatchDriversV1Completer::Sync& completer) override {
    auto match = match_callback_(request->args);
    if (match.status_value() != ZX_OK) {
      completer.ReplyError(match.status_value());
      return;
    }

    fidl::Arena arena;
    auto driver = GetMatchedDriver(arena, match.value());
    completer.ReplySuccess(
        fidl::VectorView<fuchsia_driver_index::wire::MatchedDriver>::FromExternal(&driver, 1));
  }

  void AddDeviceGroup(AddDeviceGroupRequestView request,
                      AddDeviceGroupCompleter::Sync& completer) override {
    completer.ReplyError(ZX_ERR_NOT_SUPPORTED);
  }

 private:
  static fuchsia_driver_index::wire::MatchedDriver GetMatchedDriver(fidl::AnyArena& arena,
                                                                    MatchResult match) {
    auto driver_info = fuchsia_driver_index::wire::MatchedDriverInfo(arena);
    driver_info.set_driver_url(fidl::ObjectView<fidl::StringView>(arena, arena, match.url));
    driver_info.set_url(fidl::ObjectView<fidl::StringView>(arena, arena, match.url));
    driver_info.set_is_fallback(match.is_fallback);

    if (!match.composite) {
      return fuchsia_driver_index::wire::MatchedDriver::WithDriver(
          fidl::ObjectView<fuchsia_driver_index::wire::MatchedDriverInfo>(arena, driver_info));
    }

    auto composite_info = fuchsia_driver_index::wire::MatchedCompositeInfo(arena);
    composite_info.set_node_index(match.composite->node_index);
    composite_info.set_num_nodes(match.composite->num_nodes);
    composite_info.set_driver_info(
        fidl::ObjectView<fuchsia_driver_index::wire::MatchedDriverInfo>(arena, driver_info));
    auto node_names = fidl::VectorView<fidl::StringView>(arena, match.composite->node_names.size());
    for (size_t i = 0; i < match.composite->node_names.size(); i++) {
      node_names[i] = fidl::StringView(arena, match.composite->node_names[i]);
    }
    composite_info.set_node_names(arena, node_names);

    return fuchsia_driver_index::wire::MatchedDriver::WithCompositeDriver(
        fidl::ObjectView<fuchsia_driver_index::wire::MatchedCompositeInfo>(arena, composite_info));
  }

  async_dispatcher_t* dispatcher_;
  MatchCallback match_callback_;
};

#endif  // SRC_DEVICES_BIN_DRIVER_MANAGER_FAKE_DRIVER_INDEX_H_

// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BIN_DRIVER_MANAGER_TESTS_FAKE_DRIVER_INDEX_H_
#define SRC_DEVICES_BIN_DRIVER_MANAGER_TESTS_FAKE_DRIVER_INDEX_H_

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
    auto topological_path = std::string(request->topological_path().get());
    if (device_group_match_.find(topological_path) == device_group_match_.end()) {
      completer.ReplyError(ZX_ERR_NOT_FOUND);
      return;
    }

    auto matched_result = device_group_match_[topological_path];
    if (!matched_result.composite) {
      completer.ReplyError(ZX_ERR_INTERNAL);
      return;
    }

    fidl::Arena arena;
    auto driver_info = GetDriverInfo(arena, matched_result);
    completer.ReplySuccess(
        GetMatchedCompositeInfo(arena, driver_info, matched_result.composite.value()));
  }

  void AddDeviceGroupMatch(std::string topological_path, MatchResult result) {
    device_group_match_[topological_path] = result;
  }

 private:
  static fuchsia_driver_index::wire::MatchedDriver GetMatchedDriver(fidl::AnyArena& arena,
                                                                    MatchResult match) {
    auto driver_info = GetDriverInfo(arena, match);

    if (!match.composite) {
      return fuchsia_driver_index::wire::MatchedDriver::WithDriver(
          fidl::ObjectView<fuchsia_driver_index::wire::MatchedDriverInfo>(arena, driver_info));
    }

    auto composite_info = GetMatchedCompositeInfo(arena, driver_info, match.composite.value());
    return fuchsia_driver_index::wire::MatchedDriver::WithCompositeDriver(
        fidl::ObjectView<fuchsia_driver_index::wire::MatchedCompositeInfo>(arena, composite_info));
  }

  static fuchsia_driver_index::wire::MatchedDriverInfo GetDriverInfo(fidl::AnyArena& arena,
                                                                     MatchResult match) {
    return fuchsia_driver_index::wire::MatchedDriverInfo::Builder(arena)
        .driver_url(fidl::ObjectView<fidl::StringView>(arena, arena, match.url))
        .url(fidl::ObjectView<fidl::StringView>(arena, arena, match.url))
        .is_fallback(match.is_fallback)
        .Build();
  }

  static fuchsia_driver_index::wire::MatchedCompositeInfo GetMatchedCompositeInfo(
      fidl::AnyArena& arena, fuchsia_driver_index::wire::MatchedDriverInfo driver_info,
      CompositeDriverInfo composite) {
    auto node_names = fidl::VectorView<fidl::StringView>(arena, composite.node_names.size());
    for (size_t i = 0; i < composite.node_names.size(); i++) {
      node_names[i] = fidl::StringView(arena, composite.node_names[i]);
    }

    return fuchsia_driver_index::wire::MatchedCompositeInfo::Builder(arena)
        .node_index(composite.node_index)
        .num_nodes(composite.num_nodes)
        .driver_info(
            fidl::ObjectView<fuchsia_driver_index::wire::MatchedDriverInfo>(arena, driver_info))
        .node_names(std::move(node_names))
        .Build();
  }

  async_dispatcher_t* dispatcher_;
  MatchCallback match_callback_;

  // Maps a MatchResult to a device group topological path. The MatchResult
  // in the form of MatchedCompositeInfo when FakeDriverIndex receives an
  // AddDeviceGroup() call for a matching topological path.
  std::unordered_map<std::string, MatchResult> device_group_match_;
};

#endif  // SRC_DEVICES_BIN_DRIVER_MANAGER_TESTS_FAKE_DRIVER_INDEX_H_

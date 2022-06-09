// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BIN_DRIVER_MANAGER_V1_DRIVER_DEVELOPMENT_H_
#define SRC_DEVICES_BIN_DRIVER_MANAGER_V1_DRIVER_DEVELOPMENT_H_

#include <fidl/fuchsia.driver.development/cpp/wire.h>
#include <lib/stdcompat/span.h>

#include "src/devices/bin/driver_manager/coordinator.h"
#include "src/devices/bin/driver_manager/device.h"
#include "src/devices/bin/driver_manager/driver.h"
#include "src/devices/bin/driver_manager/v1/unbind_task.h"

class DriverInfoIterator : public fidl::WireServer<fuchsia_driver_development::DriverInfoIterator> {
 public:
  explicit DriverInfoIterator(std::unique_ptr<fidl::Arena<512>> arena,
                              std::vector<fuchsia_driver_development::wire::DriverInfo> list)
      : arena_(std::move(arena)), list_(std::move(list)) {}

  void GetNext(GetNextRequestView request, GetNextCompleter::Sync& completer) override {
    constexpr size_t kMaxEntries = 100;
    auto result = cpp20::span(&list_[offset_], std::min(kMaxEntries, list_.size() - offset_));
    offset_ += result.size();

    completer.Reply(fidl::VectorView<fuchsia_driver_development::wire::DriverInfo>::FromExternal(
        result.data(), result.size()));
  }

 private:
  size_t offset_ = 0;
  std::unique_ptr<fidl::Arena<512>> arena_;
  std::vector<fuchsia_driver_development::wire::DriverInfo> list_;
};

class DeviceInfoIterator : public fidl::WireServer<fuchsia_driver_development::DeviceInfoIterator> {
 public:
  explicit DeviceInfoIterator(std::unique_ptr<fidl::Arena<512>> arena,
                              std::vector<fuchsia_driver_development::wire::DeviceInfo> list)
      : arena_(std::move(arena)), list_(std::move(list)) {}

  void GetNext(GetNextRequestView request, GetNextCompleter::Sync& completer) override {
    constexpr size_t kMaxEntries = 100;
    auto result = cpp20::span(&list_[offset_], std::min(kMaxEntries, list_.size() - offset_));
    offset_ += result.size();

    completer.Reply(fidl::VectorView<fuchsia_driver_development::wire::DeviceInfo>::FromExternal(
        result.data(), result.size()));
  }

 private:
  size_t offset_ = 0;
  std::unique_ptr<fidl::Arena<512>> arena_;
  std::vector<fuchsia_driver_development::wire::DeviceInfo> list_;
};

zx::status<std::vector<fuchsia_driver_development::wire::DriverInfo>> GetDriverInfo(
    fidl::AnyArena& allocator, const std::vector<const Driver*>& drivers);

zx::status<std::vector<fuchsia_driver_development::wire::DeviceInfo>> GetDeviceInfo(
    fidl::AnyArena& allocator, const std::vector<fbl::RefPtr<Device>>& devices);

#endif  // SRC_DEVICES_BIN_DRIVER_MANAGER_V1_DRIVER_DEVELOPMENT_H_

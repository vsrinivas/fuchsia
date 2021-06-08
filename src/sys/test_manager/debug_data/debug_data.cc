// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "debug_data.h"

#include <lib/syslog/cpp/macros.h>
#include <zircon/assert.h>
#include <zircon/errors.h>

#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "event_stream.h"

DebugDataImpl::DebugDataImpl() : weak_factory_(this) {}

DebugDataImpl::~DebugDataImpl() = default;

void DebugDataImpl::AddData(const std::string& moniker, const std::string& test_url,
                            std::string data_sink, zx::vmo vmo) {
  auto it = data_.find(moniker);
  auto data_sink_data = DataSinkDump{.data_sink = std::move(data_sink), .vmo = std::move(vmo)};
  if (it != data_.end()) {
    it->second.second.push_back(std::move(data_sink_data));
    return;
  }
  std::vector<DataSinkDump> data_sink_data_vec;
  data_sink_data_vec.push_back(std::move(data_sink_data));
  auto pair = std::make_pair(test_url, std::move(data_sink_data_vec));
  data_.emplace(moniker, std::move(pair));
}

std::optional<DebugInfo> DebugDataImpl::TakeData(const std::string& moniker) {
  auto it = data_.find(moniker);
  if (it != data_.end()) {
    auto info = std::move(it->second);
    data_.erase(it);
    return info;
  }
  return std::nullopt;
}

DebugDataImpl::Inner::Inner(fidl::InterfaceRequest<fuchsia::debugdata::DebugData> request,
                            fxl::WeakPtr<DebugDataImpl> parent, std::string moniker,
                            std::string test_url, NotifyOnClose notify,
                            async_dispatcher_t* dispatcher)
    : test_url_(std::move(test_url)),
      moniker_(std::move(moniker)),
      notify_(std::move(notify)),
      parent_(std::move(parent)),
      binding_(this, std::move(request), dispatcher) {
  binding_.set_error_handler([this](zx_status_t /*unused*/) { DestroyAndNotify(); });
}

DebugDataImpl::Inner::~Inner() = default;

std::unique_ptr<DebugDataImpl::Inner> DebugDataImpl::Remove(DebugDataImpl::Inner* ptr) {
  auto search = inners_.find(ptr);
  if (search != inners_.end()) {
    auto inner = std::move(search->second);
    inners_.erase(search);
    return inner;
  }
  return nullptr;
}

void DebugDataImpl::Inner::Publish(
    ::std::string data_sink, zx::vmo data,
    fidl::InterfaceRequest<fuchsia::debugdata::DebugDataVmoToken> /*unused*/) {
  FX_CHECK(parent_) << "parent object should not die before this object";
  parent_->AddData(moniker_, test_url_, std::move(data_sink), std::move(data));
}

void DebugDataImpl::Inner::LoadConfig(::std::string config_name, LoadConfigCallback callback) {
  FX_LOGS(WARNING) << "LoadConfig Called but is not implemented";
  CloseConnection(ZX_ERR_NOT_SUPPORTED);
}

void DebugDataImpl::Inner::CloseConnection(zx_status_t epitaph_value) {
  binding_.Close(epitaph_value);
  DestroyAndNotify();
}

void DebugDataImpl::Inner::DestroyAndNotify() {
  std::unique_ptr<DebugDataImpl::Inner> self;
  if (parent_) {
    self = parent_->Remove(this);
  }
  if (notify_ != nullptr) {
    notify_(std::move(moniker_));
  }
  // this object dies after this line
}

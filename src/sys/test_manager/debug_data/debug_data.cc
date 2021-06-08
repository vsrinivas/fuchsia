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

DebugDataImpl::DebugDataImpl(async_dispatcher_t* dispatcher,
                             std::unique_ptr<AbstractDataProcessor> data_processor)
    : weak_factory_(this), data_processor_(std::move(data_processor)), dispatcher_(dispatcher) {}

DebugDataImpl::~DebugDataImpl() = default;

void DebugDataImpl::AddData(
    const std::string& moniker, const std::string& test_url, std::string data_sink, zx::vmo vmo,
    fidl::InterfaceRequest<fuchsia::debugdata::DebugDataVmoToken> vmo_token) {
  zx::channel token_channel = vmo_token.TakeChannel();
  auto data_sink_data = DataSinkDump{.data_sink = std::move(data_sink), .vmo = std::move(vmo)};
  std::string url_owned(test_url);

  // Using a shared pointer here ensures WaitOnce has a reference to itself, preventing it from
  // going out of scope.
  FX_LOGS(DEBUG) << "Got VMO for " << url_owned;
  auto wait = std::make_shared<async::WaitOnce>(token_channel.get(), ZX_CHANNEL_PEER_CLOSED);
  wait->Begin(dispatcher_,
              [this, wait_cpy = wait, data_sink_data = std::move(data_sink_data),
               url_owned = std::move(url_owned), token_channel = std::move(token_channel)](
                  async_dispatcher_t*, async::WaitOnce*, zx_status_t status,
                  const zx_packet_signal_t* signal) mutable {
                if (status != ZX_OK) {
                  FX_LOGS(WARNING) << "Error while waiting for VMO token to close: " << status;
                }
                token_channel.reset();
                FX_LOGS(DEBUG) << "Processing VMO for " << url_owned;
                data_processor_->ProcessData(std::move(url_owned), std::move(data_sink_data));
              });
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
    fidl::InterfaceRequest<fuchsia::debugdata::DebugDataVmoToken> token) {
  FX_CHECK(parent_) << "parent object should not die before this object";
  parent_->AddData(moniker_, test_url_, std::move(data_sink), std::move(data), std::move(token));
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

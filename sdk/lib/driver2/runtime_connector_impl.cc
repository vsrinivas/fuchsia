// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/driver2/runtime_connector_impl.h>
#include <lib/fidl/cpp/wire/string_view.h>

namespace fdf {
using namespace fuchsia_driver_framework;
}  // namespace fdf

namespace {

class RuntimeProtocolIterator : public fidl::WireServer<fdf::RuntimeProtocolIterator> {
 public:
  static constexpr size_t kProtocolsBufferSize = 512;

  explicit RuntimeProtocolIterator(std::unique_ptr<fidl::Arena<kProtocolsBufferSize>> arena,
                                   std::vector<fidl::StringView> list)
      : arena_(std::move(arena)), list_(std::move(list)) {}

  void GetNext(GetNextCompleter::Sync& completer) {
    constexpr size_t kMaxEntries = fdf::wire::kMaxProtocolsPerBatch;
    auto result =
        cpp20::span(list_.begin() + offset_, std::min(kMaxEntries, list_.size() - offset_));
    offset_ += result.size();

    completer.Reply(fidl::VectorView<fidl::StringView>::FromExternal(result.data(), result.size()));
  }

 private:
  std::unique_ptr<fidl::Arena<kProtocolsBufferSize>> arena_;
  size_t offset_ = 0;
  std::vector<fidl::StringView> list_;
};

}  // namespace

namespace driver {

void RuntimeConnectorImpl::RegisterProtocol(const std::string protocol_name,
                                            RegisterProtocolHandler handler) {
  protocol_to_handler_[protocol_name] = std::move(handler);
}

void RuntimeConnectorImpl::ListProtocols(ListProtocolsRequestView request,
                                         ListProtocolsCompleter::Sync& completer) {
  auto arena = std::make_unique<fidl::Arena<RuntimeProtocolIterator::kProtocolsBufferSize>>();
  std::vector<fidl::StringView> protocols;

  for (auto& iter : protocol_to_handler_) {
    fidl::StringView sv(*arena, iter.first);
    protocols.push_back(std::move(sv));
  }

  auto iterator = std::make_unique<RuntimeProtocolIterator>(std::move(arena), std::move(protocols));
  fidl::BindServer(dispatcher_, std::move(request->iterator), std::move(iterator));
}

void RuntimeConnectorImpl::Connect(ConnectRequestView request, ConnectCompleter::Sync& completer) {
  fdf::Channel channel(request->runtime_protocol.handle);
  auto protocol_name = std::string(request->protocol_name.data(), request->protocol_name.size());
  auto it = protocol_to_handler_.find(protocol_name);
  if (it == protocol_to_handler_.end()) {
    completer.ReplyError(ZX_ERR_NOT_SUPPORTED);
    return;
  }
  auto& handler = it->second;
  zx_status_t status = handler(std::move(channel));
  if (status != ZX_OK) {
    completer.ReplyError(status);
    return;
  }
  completer.ReplySuccess();
}

}  // namespace driver

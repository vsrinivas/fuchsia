// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.boot/cpp/wire.h>
#include <lib/fidl-async/cpp/bind.h>
#include <lib/fidl/llcpp/string_view.h>
#include <lib/fidl/llcpp/vector_view.h>
#include <zircon/status.h>

#include <map>
#include <vector>

#include <fbl/string.h>
#include <fbl/string_printf.h>
#include <mock-boot-arguments/server.h>

namespace mock_boot_arguments {

void Server::CreateClient(async_dispatcher* dispatcher,
                          fidl::WireSyncClient<fuchsia_boot::Arguments>* argclient) {
  zx::channel local, remote;
  zx_status_t status = zx::channel::create(0, &local, &remote);
  if (status != ZX_OK) {
    printf(
        "mock_boot_arguments: failed to create client for mock boot arguments, failed to create "
        "channel: %s\n",
        zx_status_get_string(status));
    *argclient = fidl::WireSyncClient<fuchsia_boot::Arguments>{zx::channel()};
    return;
  }

  status = fidl::BindSingleInFlightOnly(dispatcher, std::move(remote), this);
  if (status != ZX_OK) {
    printf(
        "mock_boot_arguments: failed to create client for mock boot arguments, failed to bind: "
        "%s\n",
        zx_status_get_string(status));
    *argclient = fidl::WireSyncClient<fuchsia_boot::Arguments>{zx::channel()};
    return;
  }

  *argclient = fidl::WireSyncClient<fuchsia_boot::Arguments>{std::move(local)};
}

void Server::GetString(GetStringRequestView request, GetStringCompleter::Sync& completer) {
  auto ret = arguments_.find(std::string{request->key.data(), request->key.size()});
  if (ret == arguments_.end()) {
    completer.Reply(fidl::StringView{});
  } else {
    completer.Reply(fidl::StringView::FromExternal(ret->second));
  }
}

void Server::GetStrings(GetStringsRequestView request, GetStringsCompleter::Sync& completer) {
  std::vector<fidl::StringView> result;
  for (uint64_t i = 0; i < request->keys.count(); i++) {
    auto ret = arguments_.find(std::string{request->keys[i].data(), request->keys[i].size()});
    if (ret == arguments_.end()) {
      result.emplace_back(fidl::StringView{});
    } else {
      result.emplace_back(fidl::StringView::FromExternal(ret->second));
    }
  }
  completer.Reply(fidl::VectorView<fidl::StringView>::FromExternal(result));
}

void Server::GetBool(GetBoolRequestView request, GetBoolCompleter::Sync& completer) {
  completer.Reply(StrToBool(request->key, request->defaultval));
}

void Server::GetBools(GetBoolsRequestView request, GetBoolsCompleter::Sync& completer) {
  // The vector<bool> optimisation means we have to use a manually-allocated array.
  std::unique_ptr<bool[]> ret = std::make_unique<bool[]>(request->keys.count());
  for (uint64_t i = 0; i < request->keys.count(); i++) {
    ret[i] = StrToBool(request->keys.data()[i].key, request->keys.data()[i].defaultval);
  }
  completer.Reply(fidl::VectorView<bool>::FromExternal(ret.get(), request->keys.count()));
}

void Server::Collect(CollectRequestView request, CollectCompleter::Sync& completer) {
  std::string match{request->prefix.data(), request->prefix.size()};
  std::vector<fbl::String> result;
  for (auto entry : arguments_) {
    if (entry.first.find_first_of(match) == 0) {
      auto pair = fbl::StringPrintf("%s=%s", entry.first.data(), entry.second.data());
      result.emplace_back(std::move(pair));
    }
  }
  std::vector<fidl::StringView> views;
  for (auto val : result) {
    views.emplace_back(fidl::StringView::FromExternal(val));
  }
  completer.Reply(fidl::VectorView<fidl::StringView>::FromExternal(views));
}

bool Server::StrToBool(const fidl::StringView& key, bool defaultval) {
  auto ret = arguments_.find(std::string{key.data(), key.size()});

  if (ret == arguments_.end()) {
    return defaultval;
  }
  if (ret->second == "off" || ret->second == "0" || ret->second == "false") {
    return false;
  }

  return true;
}

}  // namespace mock_boot_arguments

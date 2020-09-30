// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/boot/llcpp/fidl.h>
#include <lib/fidl-async/cpp/bind.h>
#include <lib/fidl/llcpp/string_view.h>
#include <lib/fidl/llcpp/vector_view.h>
#include <zircon/status.h>

#include <map>

#include <fbl/string.h>
#include <fbl/string_printf.h>
#include <mock-boot-arguments/server.h>

namespace mock_boot_arguments {

void Server::CreateClient(async_dispatcher* dispatcher,
                          llcpp::fuchsia::boot::Arguments::SyncClient* argclient) {
  zx::channel local, remote;
  zx_status_t status = zx::channel::create(0, &local, &remote);
  if (status != ZX_OK) {
    printf(
        "mock_boot_arguments: failed to create client for mock boot arguments, failed to create "
        "channel: %s\n",
        zx_status_get_string(status));
    *argclient = llcpp::fuchsia::boot::Arguments::SyncClient{zx::channel()};
    return;
  }

  status = fidl::BindSingleInFlightOnly(dispatcher, std::move(remote), this);
  if (status != ZX_OK) {
    printf(
        "mock_boot_arguments: failed to create client for mock boot arguments, failed to bind: "
        "%s\n",
        zx_status_get_string(status));
    *argclient = llcpp::fuchsia::boot::Arguments::SyncClient{zx::channel()};
    return;
  }

  *argclient = llcpp::fuchsia::boot::Arguments::SyncClient{std::move(local)};
}

void Server::GetString(fidl::StringView view, GetStringCompleter::Sync& completer) {
  auto ret = arguments.find(std::string{view.data(), view.size()});
  if (ret == arguments.end()) {
    completer.Reply(fidl::StringView{});
  } else {
    completer.Reply(fidl::unowned_str(ret->second));
  }
}

void Server::GetStrings(fidl::VectorView<fidl::StringView> keys,
                        GetStringsCompleter::Sync& completer) {
  std::vector<fidl::StringView> result;
  for (uint64_t i = 0; i < keys.count(); i++) {
    auto ret = arguments.find(std::string{keys[i].data(), keys[i].size()});
    if (ret == arguments.end()) {
      result.emplace_back(fidl::StringView{});
    } else {
      result.emplace_back(fidl::unowned_str(ret->second));
    }
  }
  completer.Reply(fidl::unowned_vec(result));
}

void Server::GetBool(fidl::StringView view, bool defaultval, GetBoolCompleter::Sync& completer) {
  completer.Reply(StrToBool(view, defaultval));
}

void Server::GetBools(fidl::VectorView<llcpp::fuchsia::boot::BoolPair> keys,
                      GetBoolsCompleter::Sync& completer) {
  // The vector<bool> optimisation means we have to use a manually-allocated array.
  std::unique_ptr<bool[]> ret = std::make_unique<bool[]>(keys.count());
  for (uint64_t i = 0; i < keys.count(); i++) {
    ret[i] = StrToBool(keys.data()[i].key, keys.data()[i].defaultval);
  }
  completer.Reply(fidl::VectorView{fidl::unowned_ptr(ret.get()), keys.count()});
}

void Server::Collect(fidl::StringView prefix, CollectCompleter::Sync& completer) {
  std::string match{prefix.data(), prefix.size()};
  std::vector<fbl::String> result;
  for (auto entry : arguments) {
    if (entry.first.find_first_of(match) == 0) {
      auto pair = fbl::StringPrintf("%s=%s", entry.first.data(), entry.second.data());
      result.emplace_back(std::move(pair));
    }
  }
  std::vector<fidl::StringView> views;
  for (auto val : result) {
    views.emplace_back(fidl::unowned_str(val));
  }
  completer.Reply(fidl::unowned_vec(views));
}

bool Server::StrToBool(const fidl::StringView& key, bool defaultval) {
  auto ret = arguments.find(std::string{key.data(), key.size()});

  if (ret == arguments.end()) {
    return defaultval;
  }
  if (ret->second == "off" || ret->second == "0" || ret->second == "false") {
    return false;
  }

  return true;
}

}  // namespace mock_boot_arguments

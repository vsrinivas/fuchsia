// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.boot/cpp/wire.h>
#include <lib/async/dispatcher.h>

#include <map>

#ifndef MOCK_BOOT_ARGUMENTS_SERVER_H_
#define MOCK_BOOT_ARGUMENTS_SERVER_H_

namespace mock_boot_arguments {

class Server final : public fidl::WireServer<fuchsia_boot::Arguments> {
 public:
  explicit Server(std::map<std::string, std::string>&& args) : arguments_{args} {}
  explicit Server() = default;

  void CreateClient(async_dispatcher* dispatcher,
                    fidl::WireSyncClient<fuchsia_boot::Arguments>* argclient);

  void GetString(GetStringRequestView request, GetStringCompleter::Sync& completer) override;
  void GetStrings(GetStringsRequestView request, GetStringsCompleter::Sync& completer) override;
  void GetBool(GetBoolRequestView request, GetBoolCompleter::Sync& completer) override;
  void GetBools(GetBoolsRequestView request, GetBoolsCompleter::Sync& completer) override;
  void Collect(CollectRequestView request, CollectCompleter::Sync& completer) override;

  std::map<std::string, std::string>& GetArgumentsMap() { return arguments_; }

 private:
  std::map<std::string, std::string> arguments_;
  bool StrToBool(const fidl::StringView& key, bool defaultval);
};

}  // namespace mock_boot_arguments

#endif  // MOCK_BOOT_ARGUMENTS_SERVER_H_

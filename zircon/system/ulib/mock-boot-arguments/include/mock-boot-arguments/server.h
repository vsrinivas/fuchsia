// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/boot/llcpp/fidl.h>
#include <lib/async/dispatcher.h>

#include <map>

#ifndef MOCK_BOOT_ARGUMENTS_SERVER_H_
#define MOCK_BOOT_ARGUMENTS_SERVER_H_

namespace mock_boot_arguments {

class Server final : public llcpp::fuchsia::boot::Arguments::Interface {
 public:
  explicit Server(std::map<std::string, std::string>&& args) : arguments{args} {}
  explicit Server() : arguments{} {}

  void CreateClient(async_dispatcher* dispatcher,
                    llcpp::fuchsia::boot::Arguments::SyncClient* argclient);

  void GetString(fidl::StringView view, GetStringCompleter::Sync& completer) override;
  void GetStrings(fidl::VectorView<fidl::StringView> keys,
                  GetStringsCompleter::Sync& completer) override;
  void GetBool(fidl::StringView view, bool defaultval, GetBoolCompleter::Sync& completer) override;
  void GetBools(fidl::VectorView<llcpp::fuchsia::boot::BoolPair> keys,
                GetBoolsCompleter::Sync& completer) override;
  void Collect(fidl::StringView prefix, CollectCompleter::Sync& completer) override;

 private:
  std::map<std::string, std::string> arguments;
  bool StrToBool(const fidl::StringView& key, bool defaultval);
};

}  // namespace mock_boot_arguments

#endif  // MOCK_BOOT_ARGUMENTS_SERVER_H_

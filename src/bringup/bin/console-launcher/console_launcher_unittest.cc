// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/bringup/bin/console-launcher/console_launcher.h"

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fidl-async/cpp/bind.h>

#include <map>

#include <zxtest/zxtest.h>

class FakeBootArgsServer final : public llcpp::fuchsia::boot::Arguments::Interface {
 public:
  FakeBootArgsServer() {}

  void SetBool(std::string key, bool value) { bools_.insert_or_assign(key, value); }

  void SetString(std::string key, std::string value) { strings_.insert_or_assign(key, value); }

  // llcpp::fuchsia::boot::Arguments::Interface methods:
  void GetString(::fidl::StringView key, GetStringCompleter::Sync& completer) {
    completer.Close(ZX_ERR_NOT_SUPPORTED);
  }

  void GetStrings(::fidl::VectorView<::fidl::StringView> keys,
                  GetStringsCompleter::Sync& completer) {
    std::vector<fidl::StringView> values;
    for (auto& key : keys) {
      auto value = strings_.find(std::string(key.data()));
      if (value != strings_.end()) {
        values.push_back(fidl::unowned_str(value->second));
      } else {
        values.push_back(fidl::StringView(nullptr, 0));
      }
    }
    completer.Reply(fidl::VectorView<fidl::StringView>(
        fidl::unowned_ptr_t<fidl::StringView>(values.data()), values.size()));
  }

  void GetBool(::fidl::StringView key, bool defaultval, GetBoolCompleter::Sync& completer) {
    bool result = defaultval;
    auto value = bools_.find(std::string(key.data()));
    if (value != bools_.end()) {
      result = value->second;
    }
    completer.Reply(result);
  }

  void GetBools(::fidl::VectorView<llcpp::fuchsia::boot::BoolPair> keys,
                GetBoolsCompleter::Sync& completer) {
    std::vector<uint8_t> values;
    for (auto& bool_pair : keys) {
      bool result = bool_pair.defaultval;
      auto value = bools_.find(std::string(bool_pair.key.data()));
      if (value != bools_.end()) {
        result = value->second;
      }
      values.push_back(result);
    }
    completer.Reply(fidl::VectorView<bool>(
        fidl::unowned_ptr_t<bool>(reinterpret_cast<bool*>(values.data())), values.size()));
  }

  void Collect(::fidl::StringView prefix, CollectCompleter::Sync& completer) {
    completer.Close(ZX_ERR_NOT_SUPPORTED);
  }

 private:
  std::map<std::string, bool> bools_;
  std::map<std::string, std::string> strings_;
};

class SystemInstanceTest : public zxtest::Test {
 public:
  SystemInstanceTest() : loop_(&kAsyncLoopConfigNoAttachToCurrentThread) {
    ASSERT_OK(loop_.StartThread());

    zx::channel client, server;
    ASSERT_OK(zx::channel::create(0, &client, &server));
    boot_args_server_.reset(new FakeBootArgsServer());
    fidl::BindSingleInFlightOnly(loop_.dispatcher(), std::move(server), boot_args_server_.get());
    boot_args_client_ = llcpp::fuchsia::boot::Arguments::SyncClient(std::move(client));
  }

  std::unique_ptr<FakeBootArgsServer> boot_args_server_;
  llcpp::fuchsia::boot::Arguments::SyncClient boot_args_client_;

 private:
  async::Loop loop_;
};

TEST_F(SystemInstanceTest, CheckBootArgParsing) {
  boot_args_server_->SetBool("kernel.shell", false);
  boot_args_server_->SetBool("console.shell", true);
  boot_args_server_->SetBool("console.is_virtio", true);
  boot_args_server_->SetString("console.path", "/test/path");
  boot_args_server_->SetString("TERM", "FAKE_TERM");

  std::optional<console_launcher::Arguments> args =
      console_launcher::GetArguments(&boot_args_client_);
  ASSERT_TRUE(args.has_value());

  ASSERT_TRUE(args->run_shell);
  ASSERT_TRUE(args->is_virtio);
  ASSERT_EQ(args->term.compare("TERM=FAKE_TERM"), 0);
  ASSERT_EQ(args->device.compare("/test/path"), 0);
}

TEST_F(SystemInstanceTest, CheckBootArgDefaultStrings) {
  boot_args_server_->SetBool("kernel.shell", true);
  boot_args_server_->SetBool("console.shell", true);
  boot_args_server_->SetBool("console.is_virtio", false);

  std::optional<console_launcher::Arguments> args =
      console_launcher::GetArguments(&boot_args_client_);
  ASSERT_TRUE(args.has_value());

  ASSERT_FALSE(args->run_shell);
  ASSERT_FALSE(args->is_virtio);
  ASSERT_EQ(args->term.compare("TERM=uart"), 0);
  ASSERT_EQ(args->device.compare("/svc/console"), 0);
}

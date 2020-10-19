// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "args.h"

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fidl-async/cpp/bind.h>

#include <map>

#include <fbl/auto_lock.h>
#include <fbl/mutex.h>
#include <zxtest/zxtest.h>

class FakeBootArgsServer final : public llcpp::fuchsia::boot::Arguments::Interface {
 public:
  FakeBootArgsServer() {}

  void SetBool(std::string key, bool value) {
    fbl::AutoLock lock(&lock_);
    bools_.insert_or_assign(key, value);
  }

  void SetString(std::string key, std::string value) {
    fbl::AutoLock lock(&lock_);
    strings_.insert_or_assign(key, value);
  }

  // llcpp::fuchsia::boot::Arguments::Interface methods:
  void GetString(::fidl::StringView key, GetStringCompleter::Sync& completer) {
    completer.Close(ZX_ERR_NOT_SUPPORTED);
  }

  void GetStrings(::fidl::VectorView<::fidl::StringView> keys,
                  GetStringsCompleter::Sync& completer) {
    fbl::AutoLock lock(&lock_);
    std::vector<fidl::StringView> values;
    for (auto& key : keys) {
      std::string key_str = std::string(key.data(), key.size());
      auto value = strings_.find(key_str);
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
    fbl::AutoLock lock(&lock_);
    bool result = defaultval;
    auto value = bools_.find(std::string(key.data()));
    if (value != bools_.end()) {
      result = value->second;
    }
    completer.Reply(result);
  }

  void GetBools(::fidl::VectorView<llcpp::fuchsia::boot::BoolPair> keys,
                GetBoolsCompleter::Sync& completer) {
    fbl::AutoLock lock(&lock_);
    std::vector<uint8_t> values;
    for (auto& bool_pair : keys) {
      bool result = bool_pair.defaultval;
      std::string key = std::string(bool_pair.key.data(), bool_pair.key.size());
      auto value = bools_.find(key);
      if (value != bools_.end()) {
        result = value->second;
      } else {
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
  fbl::Mutex lock_;
  std::map<std::string, bool> bools_ __TA_GUARDED(lock_);
  std::map<std::string, std::string> strings_ __TA_GUARDED(lock_);
};

class ArgsTest : public zxtest::Test {
 public:
  ArgsTest() : loop_(&kAsyncLoopConfigNoAttachToCurrentThread) { ASSERT_OK(loop_.StartThread()); }

  void SetUp() override {
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

TEST_F(ArgsTest, CheckBootBools) {
  boot_args_server_->SetBool("virtcon.disable", true);
  boot_args_server_->SetBool("virtcon.keep-log-visible", true);
  boot_args_server_->SetBool("virtcon.keyrepeat", true);
  boot_args_server_->SetBool("virtcon.hide-on-boot", true);

  Arguments args;
  ASSERT_EQ(ParseArgs(boot_args_client_, &args), ZX_OK);

  ASSERT_TRUE(args.disable);
  ASSERT_TRUE(args.repeat_keys);
  ASSERT_TRUE(args.keep_log_visible);
  ASSERT_TRUE(args.hide_on_boot);
}

TEST_F(ArgsTest, CheckColorScheme) {
  // Default scheme.
  Arguments args;
  ASSERT_EQ(ParseArgs(boot_args_client_, &args), ZX_OK);
  ASSERT_EQ(args.color_scheme->front, 0x0F);
  ASSERT_EQ(args.color_scheme->back, 0x00);

  // Dark Scheme.
  {
    boot_args_server_->SetString("virtcon.colorscheme", "dark");
    Arguments args;
    ASSERT_EQ(ParseArgs(boot_args_client_, &args), ZX_OK);
    ASSERT_EQ(args.color_scheme->front, 0x0F);
    ASSERT_EQ(args.color_scheme->back, 0x00);
  }

  // Light Scheme.
  {
    boot_args_server_->SetString("virtcon.colorscheme", "light");
    Arguments args;
    ASSERT_EQ(ParseArgs(boot_args_client_, &args), ZX_OK);
    ASSERT_EQ(args.color_scheme->front, 0x00);
    ASSERT_EQ(args.color_scheme->back, 0x0F);
  }

  // Special Scheme.
  {
    boot_args_server_->SetString("virtcon.colorscheme", "special");
    Arguments args;
    ASSERT_EQ(ParseArgs(boot_args_client_, &args), ZX_OK);
    ASSERT_EQ(args.color_scheme->front, 0x0F);
    ASSERT_EQ(args.color_scheme->back, 0x04);
  }

  // Nonsense string == default scheme
  {
    boot_args_server_->SetString("virtcon.colorscheme", "myamazingtheme");
    Arguments args;
    ASSERT_EQ(ParseArgs(boot_args_client_, &args), ZX_OK);
    ASSERT_EQ(args.color_scheme->front, 0x0F);
    ASSERT_EQ(args.color_scheme->back, 0x00);
  }
}

TEST_F(ArgsTest, CheckFont) {
  // Default
  Arguments args;
  ASSERT_EQ(ParseArgs(boot_args_client_, &args), ZX_OK);
  ASSERT_EQ(args.font, &gfx_font_9x16);

  // 9x16
  {
    boot_args_server_->SetString("virtcon.font", "9x16");
    Arguments args;
    ASSERT_EQ(ParseArgs(boot_args_client_, &args), ZX_OK);
    ASSERT_EQ(args.font, &gfx_font_9x16);
  }

  // 18x32
  {
    boot_args_server_->SetString("virtcon.font", "18x32");
    Arguments args;
    ASSERT_EQ(ParseArgs(boot_args_client_, &args), ZX_OK);
    ASSERT_EQ(args.font, &gfx_font_18x32);
  }

  // Nonsense string == default
  {
    boot_args_server_->SetString("virtcon.font", "ONEMILLION");
    Arguments args;
    ASSERT_EQ(ParseArgs(boot_args_client_, &args), ZX_OK);
    ASSERT_EQ(args.font, &gfx_font_9x16);
  }
}

TEST_F(ArgsTest, CheckKeymap) {
  // Default
  Arguments args;
  ASSERT_EQ(ParseArgs(boot_args_client_, &args), ZX_OK);
  ASSERT_EQ(args.keymap, qwerty_map);

  // qwerty
  {
    boot_args_server_->SetString("virtcon.keymap", "qwerty");
    Arguments args;
    ASSERT_EQ(ParseArgs(boot_args_client_, &args), ZX_OK);
    ASSERT_EQ(args.keymap, qwerty_map);
  }

  // dvorak
  {
    boot_args_server_->SetString("virtcon.keymap", "dvorak");
    Arguments args;
    ASSERT_EQ(ParseArgs(boot_args_client_, &args), ZX_OK);
    ASSERT_EQ(args.keymap, dvorak_map);
  }

  // nonsense string == defaul
  {
    boot_args_server_->SetString("virtcon.keymap", "randomizedlayout");
    Arguments args;
    ASSERT_EQ(ParseArgs(boot_args_client_, &args), ZX_OK);
    ASSERT_EQ(args.keymap, qwerty_map);
  }
}

TEST_F(ArgsTest, CheckNetbootConfig) {
  boot_args_server_->SetBool("netsvc.disable", false);
  boot_args_server_->SetBool("netsvc.netboot", true);
  boot_args_server_->SetBool("devmgr.require-system", true);
  Arguments args;
  ASSERT_EQ(ParseArgs(boot_args_client_, &args), ZX_OK);
  ASSERT_EQ(args.command, "dlog -f -t");
  ASSERT_EQ(args.shells, 3);
  // Check that it's the special color scheme.
  ASSERT_EQ(args.color_scheme->front, 0x0F);
  ASSERT_EQ(args.color_scheme->back, 0x04);
}

TEST_F(ArgsTest, CheckShells) {
  // Default.
  Arguments args;
  ASSERT_EQ(ParseArgs(boot_args_client_, &args), ZX_OK);
  ASSERT_EQ(args.shells, 3);

  // Require System.
  {
    boot_args_server_->SetBool("devmgr.require-system", true);
    Arguments args;
    ASSERT_EQ(ParseArgs(boot_args_client_, &args), ZX_OK);
    ASSERT_EQ(args.shells, 0);
  }
}

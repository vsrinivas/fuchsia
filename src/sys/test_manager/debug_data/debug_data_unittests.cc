// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/debugdata/cpp/fidl.h>
#include <lib/fidl/cpp/interface_ptr.h>
#include <lib/zx/time.h>
#include <zircon/rights.h>

#include <optional>

#include <gtest/gtest.h>
#include <src/lib/fxl/strings/string_printf.h>
#include <src/lib/testing/loop_fixture/real_loop_fixture.h>

#include "debug_data.h"

using DebugDataTest = gtest::RealLoopFixture;

TEST_F(DebugDataTest, ConnectAndPublish) {
  DebugDataImpl debug_data;
  fuchsia::debugdata::DebugDataPtr debug_data_proxy;
  std::string moniker = "test_moniker";
  debug_data.BindChannel(debug_data_proxy.NewRequest(dispatcher()).TakeChannel(), moniker,
                         "test_url", dispatcher());
  RunLoopUntilIdle();

  auto info = debug_data.TakeData(moniker);
  ASSERT_FALSE(info.has_value());
  zx::vmo vmo, vmo_clone;
  ASSERT_EQ(ZX_OK, zx::vmo::create(1024, 0, &vmo));
  ASSERT_EQ(ZX_OK, vmo.duplicate(ZX_RIGHT_SAME_RIGHTS, &vmo_clone));
  fuchsia::debugdata::DebugDataVmoTokenPtr token_proxy;
  debug_data_proxy->Publish("data_sink", std::move(vmo_clone),
                            token_proxy.NewRequest(dispatcher()));

  RunLoopUntilIdle();
  std::string message = "msg1";
  ASSERT_EQ(ZX_OK, vmo.write(message.c_str(), 0, message.length()));

  info = debug_data.TakeData(moniker);
  ASSERT_TRUE(info.has_value());

  EXPECT_EQ(info->first, "test_url");
  ASSERT_EQ(info->second.size(), 1u);

  auto data = std::move(info->second[0]);
  EXPECT_EQ(data.data_sink, "data_sink");

  char bytes[100] = {0};
  ASSERT_EQ(ZX_OK, data.vmo.read(&bytes, 0, message.length()));
  EXPECT_EQ(message, bytes);

  // make sure Take Data removed the entry from map.
  info = debug_data.TakeData(moniker);
  ASSERT_FALSE(info.has_value());
}

TEST_F(DebugDataTest, MultiplePublish) {
  DebugDataImpl debug_data;
  fuchsia::debugdata::DebugDataPtr debug_data_proxy[3];
  std::string monikers[3];
  std::string test_urls[3];
  for (int i = 0; i < 3; i++) {
    monikers[i] = fxl::StringPrintf("test_moniker_%d", i);
    test_urls[i] = fxl::StringPrintf("test_url_%d", i);
    debug_data.Bind(debug_data_proxy[i].NewRequest(dispatcher()), monikers[i], test_urls[i],
                    dispatcher());
  }
  RunLoopUntilIdle();

  for (const auto& moniker : monikers) {
    auto info = debug_data.TakeData(moniker);
    ASSERT_FALSE(info.has_value());
  }

  uint times[] = {3, 2, 4};
  zx::vmo vmo;
  ASSERT_EQ(ZX_OK, zx::vmo::create(1024, 0, &vmo));
  for (int i = 0; i < 3; i++) {
    for (uint j = 0; j < times[i]; j++) {
      zx::vmo vmo_clone;
      ASSERT_EQ(ZX_OK, vmo.duplicate(ZX_RIGHT_SAME_RIGHTS, &vmo_clone));
      fuchsia::debugdata::DebugDataVmoTokenPtr token_proxy;
      debug_data_proxy[i]->Publish(fxl::StringPrintf("data_sink_%d", j), std::move(vmo_clone),
                                   token_proxy.NewRequest(dispatcher()));
      RunLoopUntilIdle();
    }
  }

  for (int i = 0; i < 3; i++) {
    auto info = debug_data.TakeData(monikers[i]);
    ASSERT_TRUE(info.has_value());
    EXPECT_EQ(info->first, test_urls[i]);
    ASSERT_EQ(info->second.size(), times[i]);
    for (uint j = 0; j < times[i]; j++) {
      auto data = std::move(info->second[j]);
      EXPECT_EQ(data.data_sink, fxl::StringPrintf("data_sink_%d", j));
    }

    // make sure Take Data removed the entry from map.
    info = debug_data.TakeData(monikers[i]);
    ASSERT_FALSE(info.has_value());
  }
}

TEST_F(DebugDataTest, PublishAfterTake) {
  DebugDataImpl debug_data;
  fuchsia::debugdata::DebugDataPtr debug_data_proxy;
  std::string moniker = "test_moniker";
  debug_data.BindChannel(debug_data_proxy.NewRequest(dispatcher()).TakeChannel(), moniker,
                         "test_url", dispatcher());
  RunLoopUntilIdle();

  auto info = debug_data.TakeData(moniker);
  ASSERT_FALSE(info.has_value());
  zx::vmo vmo, vmo_clone;
  ASSERT_EQ(ZX_OK, zx::vmo::create(1024, 0, &vmo));
  ASSERT_EQ(ZX_OK, vmo.duplicate(ZX_RIGHT_SAME_RIGHTS, &vmo_clone));
  fuchsia::debugdata::DebugDataVmoTokenPtr token_proxy;
  debug_data_proxy->Publish("data_sink", std::move(vmo_clone),
                            token_proxy.NewRequest(dispatcher()));

  RunLoopUntilIdle();

  info = debug_data.TakeData(moniker);
  ASSERT_TRUE(info.has_value());

  EXPECT_EQ(info->first, "test_url");
  ASSERT_EQ(info->second.size(), 1u);

  auto data = std::move(info->second[0]);
  EXPECT_EQ(data.data_sink, "data_sink");

  // make sure Take Data removed the entry from map.
  info = debug_data.TakeData(moniker);
  ASSERT_FALSE(info.has_value());

  ASSERT_EQ(ZX_OK, vmo.duplicate(ZX_RIGHT_SAME_RIGHTS, &vmo_clone));
  fuchsia::debugdata::DebugDataVmoTokenPtr token_proxy_2;
  debug_data_proxy->Publish("data_sink1", std::move(vmo_clone),
                            token_proxy_2.NewRequest(dispatcher()));

  RunLoopUntilIdle();

  info = debug_data.TakeData(moniker);
  ASSERT_TRUE(info.has_value());

  EXPECT_EQ(info->first, "test_url");
  ASSERT_EQ(info->second.size(), 1u);

  data = std::move(info->second[0]);
  EXPECT_EQ(data.data_sink, "data_sink1");

  // make sure Take Data removed the entry from map.
  info = debug_data.TakeData(moniker);
  ASSERT_FALSE(info.has_value());
}

TEST_F(DebugDataTest, LoadConfig) {
  DebugDataImpl debug_data;
  fuchsia::debugdata::DebugDataPtr debug_data_proxy;
  std::string moniker = "test_moniker";
  std::string notified_moniker;
  debug_data.BindChannel(
      debug_data_proxy.NewRequest(dispatcher()).TakeChannel(), moniker, "test_url", dispatcher(),
      [&notified_moniker](std::string moniker) { notified_moniker = std::move(moniker); });
  RunLoopUntilIdle();
  zx_status_t status = ZX_OK;
  debug_data_proxy.set_error_handler([&status](zx_status_t s) { status = s; });
  debug_data_proxy->LoadConfig("config", [](zx::vmo vmo) {});

  RunLoopUntilIdle();

  EXPECT_EQ(notified_moniker, moniker);
  EXPECT_EQ(status, ZX_ERR_NOT_SUPPORTED);
}

TEST_F(DebugDataTest, NotifyOnClose) {
  DebugDataImpl debug_data;
  fuchsia::debugdata::DebugDataPtr debug_data_proxy;
  std::string moniker = "test_moniker";
  std::string notified_moniker;
  debug_data.BindChannel(
      debug_data_proxy.NewRequest(dispatcher()).TakeChannel(), moniker, "test_url", dispatcher(),
      [&notified_moniker](std::string moniker) { notified_moniker = std::move(moniker); });
  RunLoopUntilIdle();

  auto info = debug_data.TakeData(moniker);
  ASSERT_FALSE(info.has_value());

  zx::vmo vmo;
  ASSERT_EQ(ZX_OK, zx::vmo::create(1024, 0, &vmo));
  fuchsia::debugdata::DebugDataVmoTokenPtr token_proxy;
  debug_data_proxy->Publish("data_sink", std::move(vmo), token_proxy.NewRequest(dispatcher()));
  debug_data_proxy.Unbind();
  RunLoopUntilIdle();

  EXPECT_EQ(notified_moniker, moniker);

  info = debug_data.TakeData(moniker);
  ASSERT_TRUE(info.has_value());

  EXPECT_EQ(info->first, "test_url");
  ASSERT_EQ(info->second.size(), 1u);

  auto data = std::move(info->second[0]);
  EXPECT_EQ(data.data_sink, "data_sink");

  // make sure Take Data removed the entry from map.
  info = debug_data.TakeData(moniker);
  ASSERT_FALSE(info.has_value());
}

// test that we don't crash when not passing notify_ callback.
TEST_F(DebugDataTest, NullNotifyOnUnbind) {
  DebugDataImpl debug_data;
  fuchsia::debugdata::DebugDataPtr debug_data_proxy;
  std::string moniker = "test_moniker";
  debug_data.BindChannel(debug_data_proxy.NewRequest(dispatcher()).TakeChannel(), moniker,
                         "test_url", dispatcher());
  RunLoopUntilIdle();

  auto info = debug_data.TakeData(moniker);
  ASSERT_FALSE(info.has_value());

  zx::vmo vmo;
  ASSERT_EQ(ZX_OK, zx::vmo::create(1024, 0, &vmo));
  fuchsia::debugdata::DebugDataVmoTokenPtr token_proxy;
  debug_data_proxy->Publish("data_sink", std::move(vmo), token_proxy.NewRequest(dispatcher()));
  debug_data_proxy.Unbind();
  RunLoopUntilIdle();

  info = debug_data.TakeData(moniker);
  ASSERT_TRUE(info.has_value());

  EXPECT_EQ(info->first, "test_url");
  ASSERT_EQ(info->second.size(), 1u);

  auto data = std::move(info->second[0]);
  EXPECT_EQ(data.data_sink, "data_sink");

  // make sure Take Data removed the entry from map.
  info = debug_data.TakeData(moniker);
  ASSERT_FALSE(info.has_value());
}

// test that we don't crash when not passing notify_ callback.
TEST_F(DebugDataTest, NullNotifyOnLoadConfig) {
  DebugDataImpl debug_data;
  fuchsia::debugdata::DebugDataPtr debug_data_proxy;
  std::string moniker = "test_moniker";
  debug_data.BindChannel(debug_data_proxy.NewRequest(dispatcher()).TakeChannel(), moniker,
                         "test_url", dispatcher());
  RunLoopUntilIdle();
  zx_status_t status = ZX_OK;
  debug_data_proxy.set_error_handler([&status](zx_status_t s) { status = s; });
  debug_data_proxy->LoadConfig("config", [](zx::vmo vmo) {});

  RunLoopUntilIdle();

  EXPECT_EQ(status, ZX_ERR_NOT_SUPPORTED);
}

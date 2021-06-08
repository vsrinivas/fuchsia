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
#include "test_data_processor.h"

using DebugDataTest = gtest::RealLoopFixture;

TEST_F(DebugDataTest, ConnectAndPublish) {
  auto shared_map = std::make_shared<TestDataProcessor::UrlDataMap>();
  std::unique_ptr<AbstractDataProcessor> data_sink_processor =
      std::make_unique<TestDataProcessor>(shared_map);
  DebugDataImpl debug_data(dispatcher(), std::move(data_sink_processor));

  fuchsia::debugdata::DebugDataPtr debug_data_proxy;
  std::string moniker = "test_moniker";
  debug_data.BindChannel(debug_data_proxy.NewRequest(dispatcher()).TakeChannel(), moniker,
                         "test_url");
  RunLoopUntilIdle();

  ASSERT_TRUE(shared_map->empty());
  zx::vmo vmo, vmo_clone;
  ASSERT_EQ(ZX_OK, zx::vmo::create(1024, 0, &vmo));
  ASSERT_EQ(ZX_OK, vmo.duplicate(ZX_RIGHT_SAME_RIGHTS, &vmo_clone));
  fuchsia::debugdata::DebugDataVmoTokenPtr token_proxy;
  debug_data_proxy->Publish("data_sink", std::move(vmo_clone),
                            token_proxy.NewRequest(dispatcher()));

  RunLoopUntilIdle();
  std::string message = "msg1";
  ASSERT_EQ(ZX_OK, vmo.write(message.c_str(), 0, message.length()));

  // Since the controller is still open, the data should not yet be processed.
  ASSERT_TRUE(shared_map->empty());

  token_proxy.Unbind();
  RunLoopUntilIdle();

  ASSERT_FALSE(shared_map->empty());
  EXPECT_EQ(shared_map->at("test_url").size(), 1u);

  auto data = std::move(shared_map->at("test_url")[0]);
  EXPECT_EQ(data.data_sink, "data_sink");

  char bytes[100] = {0};
  ASSERT_EQ(ZX_OK, data.vmo.read(&bytes, 0, message.length()));
  EXPECT_EQ(message, bytes);
}

TEST_F(DebugDataTest, MultiplePublish) {
  auto shared_map = std::make_shared<TestDataProcessor::UrlDataMap>();
  std::unique_ptr<AbstractDataProcessor> data_sink_processor =
      std::make_unique<TestDataProcessor>(shared_map);
  DebugDataImpl debug_data(dispatcher(), std::move(data_sink_processor));

  fuchsia::debugdata::DebugDataPtr debug_data_proxy[3];
  std::string monikers[3];
  std::string test_urls[3];
  for (int i = 0; i < 3; i++) {
    monikers[i] = fxl::StringPrintf("test_moniker_%d", i);
    test_urls[i] = fxl::StringPrintf("test_url_%d", i);
    debug_data.Bind(debug_data_proxy[i].NewRequest(dispatcher()), monikers[i], test_urls[i]);
  }
  RunLoopUntilIdle();

  ASSERT_TRUE(shared_map->empty());

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
      token_proxy.Unbind();
      RunLoopUntilIdle();
    }
  }

  for (int i = 0; i < 3; i++) {
    auto& data_dump_vec = shared_map->at(test_urls[i]);
    ASSERT_EQ(data_dump_vec.size(), times[i]);
    for (uint j = 0; j < times[i]; j++) {
      ASSERT_EQ(data_dump_vec[j].data_sink, fxl::StringPrintf("data_sink_%d", j));
    }
  }
}

TEST_F(DebugDataTest, PublishAfterTake) {
  auto shared_map = std::make_shared<TestDataProcessor::UrlDataMap>();
  std::unique_ptr<AbstractDataProcessor> data_sink_processor =
      std::make_unique<TestDataProcessor>(shared_map);
  DebugDataImpl debug_data(dispatcher(), std::move(data_sink_processor));

  fuchsia::debugdata::DebugDataPtr debug_data_proxy;
  std::string moniker = "test_moniker";
  std::string url = "test_url";
  debug_data.BindChannel(debug_data_proxy.NewRequest(dispatcher()).TakeChannel(), moniker, url);
  RunLoopUntilIdle();

  ASSERT_TRUE(shared_map->empty());
  zx::vmo vmo, vmo_clone;
  ASSERT_EQ(ZX_OK, zx::vmo::create(1024, 0, &vmo));
  ASSERT_EQ(ZX_OK, vmo.duplicate(ZX_RIGHT_SAME_RIGHTS, &vmo_clone));
  fuchsia::debugdata::DebugDataVmoTokenPtr token_proxy;
  debug_data_proxy->Publish("data_sink", std::move(vmo_clone),
                            token_proxy.NewRequest(dispatcher()));
  token_proxy.Unbind();

  RunLoopUntilIdle();
  auto data = std::move(shared_map->at(url));
  ASSERT_EQ(data.size(), 1u);
  ASSERT_EQ(data[0].data_sink, "data_sink");

  shared_map->clear();

  ASSERT_EQ(ZX_OK, vmo.duplicate(ZX_RIGHT_SAME_RIGHTS, &vmo_clone));
  fuchsia::debugdata::DebugDataVmoTokenPtr token_proxy_2;
  debug_data_proxy->Publish("data_sink1", std::move(vmo_clone),
                            token_proxy_2.NewRequest(dispatcher()));
  token_proxy_2.Unbind();

  RunLoopUntilIdle();
  data = std::move(shared_map->at(url));
  ASSERT_EQ(data.size(), 1u);
  ASSERT_EQ(data[0].data_sink, "data_sink1");
  shared_map->clear();
}

TEST_F(DebugDataTest, LoadConfig) {
  auto shared_map = std::make_shared<TestDataProcessor::UrlDataMap>();
  std::unique_ptr<AbstractDataProcessor> data_sink_processor =
      std::make_unique<TestDataProcessor>(shared_map);
  DebugDataImpl debug_data(dispatcher(), std::move(data_sink_processor));

  fuchsia::debugdata::DebugDataPtr debug_data_proxy;
  std::string moniker = "test_moniker";
  std::string notified_moniker;
  debug_data.BindChannel(
      debug_data_proxy.NewRequest(dispatcher()).TakeChannel(), moniker, "test_url",
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
  auto shared_map = std::make_shared<TestDataProcessor::UrlDataMap>();
  std::unique_ptr<AbstractDataProcessor> data_sink_processor =
      std::make_unique<TestDataProcessor>(shared_map);
  DebugDataImpl debug_data(dispatcher(), std::move(data_sink_processor));

  fuchsia::debugdata::DebugDataPtr debug_data_proxy;
  std::string moniker = "test_moniker";
  std::string notified_moniker;
  debug_data.BindChannel(
      debug_data_proxy.NewRequest(dispatcher()).TakeChannel(), moniker, "test_url",
      [&notified_moniker](std::string moniker) { notified_moniker = std::move(moniker); });
  RunLoopUntilIdle();

  ASSERT_TRUE(shared_map->empty());

  zx::vmo vmo;
  ASSERT_EQ(ZX_OK, zx::vmo::create(1024, 0, &vmo));
  fuchsia::debugdata::DebugDataVmoTokenPtr token_proxy;
  debug_data_proxy->Publish("data_sink", std::move(vmo), token_proxy.NewRequest(dispatcher()));
  debug_data_proxy.Unbind();
  token_proxy.Unbind();
  RunLoopUntilIdle();

  EXPECT_EQ(notified_moniker, moniker);

  ASSERT_EQ(shared_map->at("test_url").size(), 1u);
  ASSERT_EQ(shared_map->at("test_url")[0].data_sink, "data_sink");
}

// test that we don't crash when not passing notify_ callback.
TEST_F(DebugDataTest, NullNotifyOnUnbind) {
  auto shared_map = std::make_shared<TestDataProcessor::UrlDataMap>();
  std::unique_ptr<AbstractDataProcessor> data_sink_processor =
      std::make_unique<TestDataProcessor>(shared_map);
  DebugDataImpl debug_data(dispatcher(), std::move(data_sink_processor));

  fuchsia::debugdata::DebugDataPtr debug_data_proxy;
  std::string moniker = "test_moniker";
  debug_data.BindChannel(debug_data_proxy.NewRequest(dispatcher()).TakeChannel(), moniker,
                         "test_url");
  RunLoopUntilIdle();

  ASSERT_TRUE(shared_map->empty());

  zx::vmo vmo;
  ASSERT_EQ(ZX_OK, zx::vmo::create(1024, 0, &vmo));
  fuchsia::debugdata::DebugDataVmoTokenPtr token_proxy;
  debug_data_proxy->Publish("data_sink", std::move(vmo), token_proxy.NewRequest(dispatcher()));
  debug_data_proxy.Unbind();
  token_proxy.Unbind();
  RunLoopUntilIdle();

  ASSERT_EQ(shared_map->at("test_url").size(), 1u);
  ASSERT_EQ(shared_map->at("test_url")[0].data_sink, "data_sink");
}

// test that we don't crash when not passing notify_ callback.
TEST_F(DebugDataTest, NullNotifyOnLoadConfig) {
  auto shared_map = std::make_shared<TestDataProcessor::UrlDataMap>();
  std::unique_ptr<AbstractDataProcessor> data_sink_processor =
      std::make_unique<TestDataProcessor>(shared_map);
  DebugDataImpl debug_data(dispatcher(), std::move(data_sink_processor));

  fuchsia::debugdata::DebugDataPtr debug_data_proxy;
  std::string moniker = "test_moniker";
  debug_data.BindChannel(debug_data_proxy.NewRequest(dispatcher()).TakeChannel(), moniker,
                         "test_url");
  RunLoopUntilIdle();
  zx_status_t status = ZX_OK;
  debug_data_proxy.set_error_handler([&status](zx_status_t s) { status = s; });
  debug_data_proxy->LoadConfig("config", [](zx::vmo vmo) {});

  RunLoopUntilIdle();

  EXPECT_EQ(status, ZX_ERR_NOT_SUPPORTED);
}

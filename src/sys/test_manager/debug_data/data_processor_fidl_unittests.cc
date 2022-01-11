// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <lib/fdio/fd.h>
#include <lib/fidl/cpp/interface_ptr.h>

#include <memory>
#include <optional>
#include <utility>

#include <gtest/gtest.h>
#include <src/lib/fxl/strings/string_printf.h>
#include <src/lib/testing/loop_fixture/real_loop_fixture.h>

#include "data_processor_fidl.h"
#include "test_data_processor.h"

namespace ftest_debug = fuchsia::test::debug;

ftest_debug::DebugVmo MakeDebugVmo(std::string test_url, std::string data_sink) {
  ftest_debug::DebugVmo result;
  result.data_sink = std::move(data_sink);
  result.test_url = std::move(test_url);
  zx::vmo::create(1024, 0, &result.vmo);
  return result;
}

using DataProcessorFidlTest = gtest::RealLoopFixture;

TEST_F(DataProcessorFidlTest, ProcessAndFinish) {
  auto shared_map = std::make_shared<TestDataProcessor::UrlDataMap>();

  bool on_done_called = false;
  ftest_debug::DebugDataProcessorPtr data_processor_fidl_proxy;
  DataProcessorFidl processor_fidl(
      data_processor_fidl_proxy.NewRequest(), [&]() { on_done_called = true; },
      [&](fbl::unique_fd) { return std::make_unique<TestDataProcessor>(shared_map); },
      dispatcher());

  int fd = open("/tmp", O_DIRECTORY | O_RDWR);
  zx::channel directory_handle;
  ASSERT_EQ(fdio_fd_transfer(fd, directory_handle.reset_and_get_address()), ZX_OK);
  fidl::InterfaceHandle<fuchsia::io::Directory> directory(std::move(directory_handle));

  data_processor_fidl_proxy->SetDirectory(std::move(directory));

  RunLoopUntilIdle();
  ASSERT_TRUE(shared_map->empty());

  std::vector<ftest_debug::DebugVmo> first_vmos;
  first_vmos.push_back(MakeDebugVmo("test-url-1", "data-sink-1"));
  first_vmos.push_back(MakeDebugVmo("test-url-2", "data-sink-2"));
  data_processor_fidl_proxy->AddDebugVmos(std::move(first_vmos), [&]() {});
  std::vector<ftest_debug::DebugVmo> second_vmos;
  second_vmos.push_back(MakeDebugVmo("test-url-3", "data-sink-3"));
  data_processor_fidl_proxy->AddDebugVmos(std::move(second_vmos), [&]() {});

  RunLoopUntilIdle();
  EXPECT_EQ(shared_map->size(), 3u);
  EXPECT_EQ(shared_map->at("test-url-1").size(), 1u);
  EXPECT_EQ(shared_map->at("test-url-1")[0].data_sink, "data-sink-1");
  EXPECT_EQ(shared_map->at("test-url-2").size(), 1u);
  EXPECT_EQ(shared_map->at("test-url-2")[0].data_sink, "data-sink-2");
  EXPECT_EQ(shared_map->at("test-url-3").size(), 1u);
  EXPECT_EQ(shared_map->at("test-url-3")[0].data_sink, "data-sink-3");

  ASSERT_FALSE(on_done_called);
  bool finish_called = false;
  data_processor_fidl_proxy->Finish([&]() { finish_called = true; });

  RunLoopUntilIdle();
  ASSERT_TRUE(on_done_called);
  ASSERT_TRUE(finish_called);
}

TEST_F(DataProcessorFidlTest, AwaitIdleOnFinish) {
  zx::event event;
  zx::event::create(0, &event);
  zx::unowned_event unowned_event = event.borrow();

  ftest_debug::DebugDataProcessorPtr data_processor_fidl_proxy;
  DataProcessorFidl processor_fidl(
      data_processor_fidl_proxy.NewRequest(), [&]() {},
      [&](fbl::unique_fd) {
        zx_handle_t event_handle = event.release();
        return std::make_unique<TestDataProcessor>(event_handle);
      },
      dispatcher());

  int fd = open("/tmp", O_DIRECTORY | O_RDWR);
  zx::channel directory_handle;
  ASSERT_EQ(fdio_fd_transfer(fd, directory_handle.reset_and_get_address()), ZX_OK);
  fidl::InterfaceHandle<fuchsia::io::Directory> directory(std::move(directory_handle));

  data_processor_fidl_proxy->SetDirectory(std::move(directory));
  std::vector<ftest_debug::DebugVmo> first_vmos;
  first_vmos.push_back(MakeDebugVmo("test-url-1", "data-sink-1"));
  first_vmos.push_back(MakeDebugVmo("test-url-2", "data-sink-2"));
  data_processor_fidl_proxy->AddDebugVmos(std::move(first_vmos), [&]() {});

  bool finish_called = false;
  data_processor_fidl_proxy->Finish([&]() { finish_called = true; });

  // finish shouldn't be called until we signal on the event we gave to the data processor.
  RunLoopUntilIdle();
  ASSERT_FALSE(finish_called);

  unowned_event->signal(0, IDLE_SIGNAL);
  RunLoopUntilIdle();
  ASSERT_TRUE(finish_called);
}

// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "payload-streamer.h"

#include <fidl/fuchsia.paver/cpp/wire.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/fidl-async/cpp/bind.h>

#include <zxtest/zxtest.h>

namespace fastboot {

namespace {
TEST(PayloadStreamerTest, RegisterVmo) {
  const char data[] = "payload streamer data";
  auto streamer_endpoints = fidl::CreateEndpoints<fuchsia_paver::PayloadStream>();
  ASSERT_TRUE(streamer_endpoints.is_ok());
  auto [client_end, server_end] = std::move(*streamer_endpoints);

  fidl::WireSyncClient<fuchsia_paver::PayloadStream> client =
      fidl::WireSyncClient(std::move(client_end));

  // Launch thread which implements interface.
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  internal::PayloadStreamer streamer(std::move(server_end), data, sizeof(data));
  loop.StartThread("fastboot-payload-stream");

  zx::vmo vmo;
  ASSERT_OK(zx::vmo::create(1, 0, &vmo));
  auto result = client->RegisterVmo(std::move(vmo));
  ASSERT_OK(result.status());
  EXPECT_OK(result.value().status);
}

TEST(PayloadStreamerTest, RegisterVmoAgainErrorsOut) {
  const char data[] = "payload streamer data";
  auto streamer_endpoints = fidl::CreateEndpoints<fuchsia_paver::PayloadStream>();
  ASSERT_TRUE(streamer_endpoints.is_ok());
  auto [client_end, server_end] = std::move(*streamer_endpoints);

  fidl::WireSyncClient<fuchsia_paver::PayloadStream> client =
      fidl::WireSyncClient(std::move(client_end));

  // Launch thread which implements interface.
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  internal::PayloadStreamer streamer(std::move(server_end), data, sizeof(data));
  loop.StartThread("fastboot-payload-stream");

  {
    zx::vmo vmo;
    ASSERT_OK(zx::vmo::create(1, 0, &vmo));
    auto result = client->RegisterVmo(std::move(vmo));
    ASSERT_OK(result.status());
    EXPECT_OK(result.value().status);
  }

  {
    zx::vmo vmo;
    ASSERT_OK(zx::vmo::create(1, 0, &vmo));
    auto result = client->RegisterVmo(std::move(vmo));
    ASSERT_OK(result.status());
    EXPECT_EQ(result.value().status, ZX_ERR_ALREADY_BOUND);
  }
}

TEST(PayloadStreamerTest, ReadData) {
  const char data[] = "payload streamer data";
  auto streamer_endpoints = fidl::CreateEndpoints<fuchsia_paver::PayloadStream>();
  ASSERT_TRUE(streamer_endpoints.is_ok());
  auto [client_end, server_end] = std::move(*streamer_endpoints);

  fidl::WireSyncClient<fuchsia_paver::PayloadStream> client =
      fidl::WireSyncClient(std::move(client_end));

  // Launch thread which implements interface.
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  internal::PayloadStreamer streamer(std::move(server_end), data, sizeof(data));
  loop.StartThread("fastboot-payload-stream");

  zx::vmo vmo, dup;
  ASSERT_OK(zx::vmo::create(sizeof(data), 0, &vmo));
  ASSERT_OK(vmo.duplicate(ZX_RIGHT_SAME_RIGHTS, &dup));
  auto register_result = client->RegisterVmo(std::move(dup));
  ASSERT_OK(register_result.status());
  EXPECT_OK(register_result.value().status);

  auto read_result = client->ReadData();
  ASSERT_OK(read_result.status());
  ASSERT_TRUE(read_result.value().result.is_info());

  char buffer[sizeof(data)] = {};
  ASSERT_EQ(read_result.value().result.info().size, sizeof(buffer));
  ASSERT_OK(vmo.read(buffer, read_result.value().result.info().offset,
                     read_result.value().result.info().size));
  ASSERT_EQ(memcmp(data, buffer, sizeof(data)), 0);

  // eof is returned if continue to read.
  auto eof_result = client->ReadData();
  ASSERT_OK(eof_result.status());
  ASSERT_TRUE(eof_result.value().result.is_eof());
}

}  // namespace

}  // namespace fastboot

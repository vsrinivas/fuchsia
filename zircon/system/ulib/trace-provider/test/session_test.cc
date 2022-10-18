// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "../session.h"

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/trace-provider/provider.h>
#include <lib/trace/event.h>

#include <zxtest/zxtest.h>

namespace trace {
namespace {

static constexpr size_t kBufferSize = 65535;
static constexpr size_t kFifoCount = 4;
static const std::string kAlertName = "alert_name";
static const std::string kAlertNameMin = "a";
static const std::string kAlertNameMax = "alert_name_max";

void CheckAlertNameAndZeroPadding(const std::string& alert_name, const void* p, size_t size) {
  const char* pchar = reinterpret_cast<const char*>(p);
  for (size_t index = 0; size > 0; --size, ++pchar, ++index) {
    if (index >= alert_name.size()) {
      ASSERT_EQ(0, *pchar);
      continue;
    }

    ASSERT_EQ(alert_name[index], *pchar);
  }
}

// Tests that alerts are send over the fifo.
TEST(SessionTest, AlertSent) {
  async::Loop loop{&kAsyncLoopConfigNoAttachToCurrentThread};

  zx::vmo buffer;
  zx_status_t status = zx::vmo::create(kBufferSize, 0, &buffer);
  ASSERT_EQ(ZX_OK, status);

  zx::fifo fifo_provider;
  zx::fifo fifo_manager;
  status = zx::fifo::create(kFifoCount, sizeof(trace_provider_packet_t), 0, &fifo_provider,
                            &fifo_manager);
  ASSERT_EQ(ZX_OK, status);

  std::vector<std::string> categories = {
      // Filter without wildcard
      "test_category",
      // Filter with wildcard
      "wildcard*",
      // Empty filter to make sure the wildcard matcher can handle the empty case
      ""};

  internal::Session::InitializeEngine(loop.dispatcher(), TRACE_BUFFERING_MODE_CIRCULAR,
                                      std::move(buffer), std::move(fifo_provider), categories);

  // Not started yet.
  TRACE_ALERT("test_category", kAlertName.c_str());

  trace_provider_packet_t packet;
  size_t actual;
  status = fifo_manager.read(sizeof(trace_provider_packet_t), &packet, 1, &actual);
  ASSERT_EQ(ZX_ERR_SHOULD_WAIT, status);

  internal::Session::StartEngine(TRACE_START_CLEAR_ENTIRE_BUFFER);

  // No alerts since start.
  status = fifo_manager.read(sizeof(trace_provider_packet_t), &packet, 1, &actual);
  ASSERT_EQ(ZX_ERR_SHOULD_WAIT, status);

  // Alert name neither min nor max length.
  TRACE_ALERT("wildcard_category", kAlertName.c_str());

  status = fifo_manager.read(sizeof(trace_provider_packet_t), &packet, 1, &actual);
  ASSERT_EQ(ZX_OK, status);
  ASSERT_EQ(1, actual);

  ASSERT_EQ(TRACE_PROVIDER_ALERT, packet.request);
  CheckAlertNameAndZeroPadding(
      kAlertName, &packet.data16,
      sizeof(packet.data16) + sizeof(packet.data32) + sizeof(packet.data64));

  // Alert name of min length (1).
  TRACE_ALERT("test_category", kAlertNameMin.c_str());

  status = fifo_manager.read(sizeof(trace_provider_packet_t), &packet, 1, &actual);
  ASSERT_EQ(ZX_OK, status);
  ASSERT_EQ(1, actual);

  ASSERT_EQ(TRACE_PROVIDER_ALERT, packet.request);
  CheckAlertNameAndZeroPadding(
      kAlertNameMin, &packet.data16,
      sizeof(packet.data16) + sizeof(packet.data32) + sizeof(packet.data64));

  // Alert name of max length (14).
  TRACE_ALERT("wildcard_category", kAlertNameMax.c_str());

  status = fifo_manager.read(sizeof(trace_provider_packet_t), &packet, 1, &actual);
  ASSERT_EQ(ZX_OK, status);
  ASSERT_EQ(1, actual);

  ASSERT_EQ(TRACE_PROVIDER_ALERT, packet.request);
  CheckAlertNameAndZeroPadding(
      kAlertNameMax, &packet.data16,
      sizeof(packet.data16) + sizeof(packet.data32) + sizeof(packet.data64));

  // Disabled category.
  TRACE_ALERT("other_category", kAlertName.c_str());

  status = fifo_manager.read(sizeof(trace_provider_packet_t), &packet, 1, &actual);
  ASSERT_EQ(ZX_ERR_SHOULD_WAIT, status);

  loop.RunUntilIdle();
  loop.Shutdown();
}

}  // namespace
}  // namespace trace

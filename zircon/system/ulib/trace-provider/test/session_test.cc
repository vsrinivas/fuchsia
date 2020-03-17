// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "../session.h"

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/trace-provider/provider.h>

#include <trace/event.h>
#include <zxtest/zxtest.h>

namespace trace {
namespace {

static constexpr size_t kBufferSize = 65535;
static constexpr size_t kFifoCount = 4;
static const std::string kTriggerName = "trigger_name_requiring_multiple_packets";

void CheckTriggerNameFragment(size_t* progress, const void* p, size_t size) {
  ASSERT_GE(kTriggerName.size(), *progress);

  const char* pchar = reinterpret_cast<const char*>(p);
  for (; size > 0; --size, ++pchar) {
    if (*progress == kTriggerName.size()) {
      ASSERT_EQ(0, *pchar);
      continue;
    }

    ASSERT_EQ(kTriggerName[*progress], *pchar);
    ++(*progress);
  }
}

// Tests that triggers are send over the fifo.
TEST(SessionTest, TriggerSent) {
  async::Loop loop{&kAsyncLoopConfigNoAttachToCurrentThread};

  zx::vmo buffer;
  zx_status_t status = zx::vmo::create(kBufferSize, 0, &buffer);
  ASSERT_EQ(ZX_OK, status);

  zx::fifo fifo_provider;
  zx::fifo fifo_manager;
  status = zx::fifo::create(kFifoCount, sizeof(trace_provider_packet_t), 0, &fifo_provider,
                            &fifo_manager);
  ASSERT_EQ(ZX_OK, status);

  std::vector<std::string> categories;

  internal::Session::InitializeEngine(loop.dispatcher(), TRACE_BUFFERING_MODE_CIRCULAR,
                                      std::move(buffer), std::move(fifo_provider), categories);

  TRACE_TRIGGER(kTriggerName.c_str());

  trace_provider_packet_t packet;
  size_t actual;
  status = fifo_manager.read(sizeof(trace_provider_packet_t), &packet, 1, &actual);
  ASSERT_EQ(ZX_ERR_SHOULD_WAIT, status);

  internal::Session::StartEngine(TRACE_START_CLEAR_ENTIRE_BUFFER);

  status = fifo_manager.read(sizeof(trace_provider_packet_t), &packet, 1, &actual);
  ASSERT_EQ(ZX_ERR_SHOULD_WAIT, status);

  TRACE_TRIGGER(kTriggerName.c_str());

  status = fifo_manager.read(sizeof(trace_provider_packet_t), &packet, 1, &actual);
  ASSERT_EQ(ZX_OK, status);
  ASSERT_EQ(1, actual);

  ASSERT_EQ(TRACE_PROVIDER_TRIGGER, packet.request);
  ASSERT_EQ(kTriggerName.size(), packet.data16);

  size_t progress = 0;

  CheckTriggerNameFragment(&progress, &packet.data32,
                           sizeof(packet.data32) + sizeof(packet.data64));

  while (progress < kTriggerName.size()) {
    status = fifo_manager.read(sizeof(trace_provider_packet_t), &packet, 1, &actual);
    ASSERT_EQ(ZX_OK, status);
    ASSERT_EQ(1, actual);

    ASSERT_EQ(TRACE_PROVIDER_TRIGGER_CONT, packet.request);

    CheckTriggerNameFragment(&progress, &packet.data16,
                             sizeof(packet.data16) + sizeof(packet.data32) + sizeof(packet.data64));
  }

  loop.RunUntilIdle();
  loop.Shutdown();
}

}  // namespace
}  // namespace trace

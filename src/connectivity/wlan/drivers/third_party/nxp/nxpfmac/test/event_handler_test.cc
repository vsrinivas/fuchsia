// Copyright (c) 2022 The Fuchsia Authors
//
// Permission to use, copy, modify, and/or distribute this software for any purpose with or without
// fee is hereby granted, provided that the above copyright notice and this permission notice
// appear in all copies.
//
// THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS
// SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE
// AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
// WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT,
// NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE
// OF THIS SOFTWARE.

#include "src/connectivity/wlan/drivers/third_party/nxp/nxpfmac/event_handler.h"

#include <zxtest/zxtest.h>

namespace {

using wlan::nxpfmac::EventHandler;

TEST(EventHandlerTest, Constructible) { ASSERT_NO_FATAL_FAILURE(EventHandler()); }

TEST(EventHandlerTest, GlobalEvent) {
  EventHandler event_handler;

  constexpr mlan_event_id kEventId = MLAN_EVENT_ID_FW_UNKNOWN;

  bool event_triggered = false;

  event_handler.RegisterForEvent(kEventId, [&](pmlan_event event) { event_triggered = true; });

  mlan_event event{
      .event_id = kEventId,
  };
  event_handler.OnEvent(&event);
  ASSERT_TRUE(event_triggered);
}

TEST(EventHandlerTest, UnrelatedGlobalEvent) {
  EventHandler event_handler;

  bool event_triggered = false;

  event_handler.RegisterForEvent(MLAN_EVENT_ID_FW_UNKNOWN,
                                 [&](pmlan_event event) { event_triggered = true; });

  // Create an event with a different event id.
  mlan_event event{
      .event_id = MLAN_EVENT_ID_DRV_DBG_DUMP,
  };
  event_handler.OnEvent(&event);
  // The event callback should not have been called.
  ASSERT_FALSE(event_triggered);
}

TEST(EventHandlerTest, InterfaceEvent) {
  EventHandler event_handler;

  constexpr mlan_event_id kEventId = MLAN_EVENT_ID_DRV_SCAN_REPORT;
  constexpr uint32_t kBssIndex = 42;

  bool correct_event_triggered = false;
  bool incorrect_event_triggered = false;

  // Register for the correct interface
  event_handler.RegisterForInterfaceEvent(
      kEventId, kBssIndex, [&](pmlan_event event) { correct_event_triggered = true; });
  // Register for another interface
  event_handler.RegisterForInterfaceEvent(
      kEventId, kBssIndex - 10, [&](pmlan_event event) { incorrect_event_triggered = true; });

  mlan_event event{
      .bss_index = kBssIndex,
      .event_id = kEventId,
  };
  event_handler.OnEvent(&event);
  // Only the event for the specific interface should have triggered.
  ASSERT_TRUE(correct_event_triggered);
  ASSERT_FALSE(incorrect_event_triggered);
}

TEST(EventHandlerTest, UnrelatedInterfaceEvent) {
  EventHandler event_handler;

  constexpr uint32_t kBssIndex = 42;

  bool event_triggered = false;

  // Register for an interface event
  event_handler.RegisterForInterfaceEvent(MLAN_EVENT_ID_DRV_SCAN_REPORT, kBssIndex,
                                          [&](pmlan_event event) { event_triggered = true; });

  // Create a different event for the same interface
  mlan_event event{
      .bss_index = kBssIndex,
      .event_id = MLAN_EVENT_ID_FW_UNKNOWN,
  };
  event_handler.OnEvent(&event);
  // The event callback should not have been called.
  ASSERT_FALSE(event_triggered);
}

}  // namespace

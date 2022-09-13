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
using wlan::nxpfmac::EventRegistration;

TEST(EventHandlerTest, Constructible) {
  // Test that an event handler can be constructed.

  ASSERT_NO_FATAL_FAILURE(EventHandler());
}

TEST(EventHandlerTest, GlobalEvent) {
  // Test that a global event (i.e. not related to an interface) works correctly.

  EventHandler event_handler;

  constexpr mlan_event_id kEventId = MLAN_EVENT_ID_FW_UNKNOWN;

  bool event_triggered = false;

  EventRegistration event_registration =
      event_handler.RegisterForEvent(kEventId, [&](pmlan_event event) { event_triggered = true; });

  mlan_event event{
      .event_id = kEventId,
  };
  event_handler.OnEvent(&event);
  ASSERT_TRUE(event_triggered);
}

TEST(EventHandlerTest, UnrelatedGlobalEvent) {
  // Test that an event handler on responds to the registered event ID.

  EventHandler event_handler;

  bool event_triggered = false;

  EventRegistration event_registration = event_handler.RegisterForEvent(
      MLAN_EVENT_ID_FW_UNKNOWN, [&](pmlan_event event) { event_triggered = true; });

  // Create an event with a different event id.
  mlan_event event{
      .event_id = MLAN_EVENT_ID_DRV_DBG_DUMP,
  };
  event_handler.OnEvent(&event);
  // The event callback should not have been called.
  ASSERT_FALSE(event_triggered);
}

TEST(EventHandlerTest, InterfaceEvent) {
  // Test that an interface event works properly and only responds for events on the interface.

  EventHandler event_handler;

  constexpr mlan_event_id kEventId = MLAN_EVENT_ID_DRV_SCAN_REPORT;
  constexpr uint32_t kBssIndex = 42;

  bool correct_event_triggered = false;
  bool incorrect_event_triggered = false;

  // Register for the correct interface
  EventRegistration first_event = event_handler.RegisterForInterfaceEvent(
      kEventId, kBssIndex, [&](pmlan_event event) { correct_event_triggered = true; });
  // Register for another interface
  EventRegistration second_event = event_handler.RegisterForInterfaceEvent(
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
  // Test that an interface event handler does not respons to an unrelated event.

  EventHandler event_handler;

  constexpr uint32_t kBssIndex = 42;

  bool event_triggered = false;

  // Register for an interface event
  EventRegistration event_registration = event_handler.RegisterForInterfaceEvent(
      MLAN_EVENT_ID_DRV_SCAN_REPORT, kBssIndex, [&](pmlan_event event) { event_triggered = true; });

  // Create a different event for the same interface
  mlan_event event{
      .bss_index = kBssIndex,
      .event_id = MLAN_EVENT_ID_FW_UNKNOWN,
  };
  event_handler.OnEvent(&event);
  // The event callback should not have been called.
  ASSERT_FALSE(event_triggered);
}

TEST(EventHandlerTest, UnregisterGlobalEvent) {
  // Test that unregistering a global event handler no longer triggers that handler.

  EventHandler event_handler;

  // Register two different event handlers.
  std::atomic first_event_triggered = false;
  std::atomic second_event_triggered = false;
  EventRegistration first_event = event_handler.RegisterForEvent(
      MLAN_EVENT_ID_DRV_SCAN_REPORT, [&](pmlan_event event) { first_event_triggered = true; });
  EventRegistration second_event = event_handler.RegisterForEvent(
      MLAN_EVENT_ID_DRV_SCAN_REPORT, [&](pmlan_event event) { second_event_triggered = true; });

  // The first call to unregister should succeed
  ASSERT_TRUE(event_handler.UnregisterEvent(std::move(first_event)));
  // The second should not
  ASSERT_FALSE(event_handler.UnregisterEvent(std::move(first_event)));

  mlan_event event{.event_id = MLAN_EVENT_ID_DRV_SCAN_REPORT};
  event_handler.OnEvent(&event);

  // The event handler that remains should still trigger.
  ASSERT_FALSE(first_event_triggered.load());
  ASSERT_TRUE(second_event_triggered.load());
}

TEST(EventHandlerTest, UnregisterInterfaceEvent) {
  // Test that unregistering an interface event handler no longer triggers that handler.

  constexpr uint32_t kBssIndex = 17;

  EventHandler event_handler;

  // Register two different event handlers.
  std::atomic<bool> first_event_triggered = false;
  std::atomic<bool> second_event_triggered = false;
  EventRegistration first_event = event_handler.RegisterForInterfaceEvent(
      MLAN_EVENT_ID_DRV_SCAN_REPORT, kBssIndex,
      [&](pmlan_event event) { first_event_triggered = true; });
  EventRegistration second_event = event_handler.RegisterForInterfaceEvent(
      MLAN_EVENT_ID_DRV_SCAN_REPORT, kBssIndex,
      [&](pmlan_event event) { second_event_triggered = true; });

  // The first call to unregister should succeed
  ASSERT_TRUE(event_handler.UnregisterEvent(std::move(second_event)));
  // The second should not
  ASSERT_FALSE(event_handler.UnregisterEvent(std::move(second_event)));

  mlan_event event{.bss_index = kBssIndex, .event_id = MLAN_EVENT_ID_DRV_SCAN_REPORT};
  event_handler.OnEvent(&event);

  // The event handler that remains should still trigger.
  ASSERT_TRUE(first_event_triggered.load());
  ASSERT_FALSE(second_event_triggered.load());
}

TEST(EventHandlerTest, UnregisterGlobalEventImplicitly) {
  // Test implicit unregistering by destruction of the event registration object for a global event.

  EventHandler event_handler;

  std::atomic first_event_triggered = false;
  std::atomic second_event_triggered = false;
  EventRegistration first_event;
  {
    // Register two different event handlers.
    first_event = event_handler.RegisterForEvent(
        MLAN_EVENT_ID_DRV_SCAN_REPORT, [&](pmlan_event event) { first_event_triggered = true; });
    EventRegistration second_event = event_handler.RegisterForEvent(
        MLAN_EVENT_ID_DRV_SCAN_REPORT, [&](pmlan_event event) { second_event_triggered = true; });
  }
  // The second event registration is now deleted, it should've been unregistered

  mlan_event event{.event_id = MLAN_EVENT_ID_DRV_SCAN_REPORT};
  event_handler.OnEvent(&event);

  // The event handler that remains should still trigger.
  ASSERT_TRUE(first_event_triggered.load());
  ASSERT_FALSE(second_event_triggered.load());
}

TEST(EventHandlerTest, UnregisterInterfaceEventImplicitly) {
  // Test implicit unregistering by destruction of the event registration object for an interface.

  constexpr uint32_t kBssIndex = 17;

  EventHandler event_handler;

  std::atomic first_event_triggered = false;
  std::atomic second_event_triggered = false;
  EventRegistration second_event;
  {
    // Register two different event handlers.
    EventRegistration first_event = event_handler.RegisterForInterfaceEvent(
        MLAN_EVENT_ID_DRV_SCAN_REPORT, kBssIndex,
        [&](pmlan_event event) { first_event_triggered = true; });
    second_event = event_handler.RegisterForInterfaceEvent(
        MLAN_EVENT_ID_DRV_SCAN_REPORT, kBssIndex,
        [&](pmlan_event event) { second_event_triggered = true; });
  }
  // The first event registration is now deleted, it should've been unregistered

  mlan_event event{.bss_index = kBssIndex, .event_id = MLAN_EVENT_ID_DRV_SCAN_REPORT};
  event_handler.OnEvent(&event);

  // The event handler that remains should still trigger.
  ASSERT_FALSE(first_event_triggered.load());
  ASSERT_TRUE(second_event_triggered.load());
}

}  // namespace

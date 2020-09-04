// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/semantics/a11y_semantics_event_manager.h"

#include <lib/syslog/cpp/macros.h>
#include <lib/zx/event.h>

#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/ui/a11y/lib/semantics/tests/mocks/mock_semantics_event_listener.h"

namespace accessibility_test {

class A11ySemanticsEventManagerTest : public ::testing::Test {
 public:
  A11ySemanticsEventManagerTest() {}

 protected:
  void SetUp() override {
    listener_ = std::make_unique<MockSemanticsEventListener>();
    a11y_semantics_event_manager_ = std::make_unique<a11y::A11ySemanticsEventManager>();
  }

  std::unique_ptr<MockSemanticsEventListener> listener_;
  std::unique_ptr<a11y::A11ySemanticsEventManager> a11y_semantics_event_manager_;
};

TEST_F(A11ySemanticsEventManagerTest, RegisterAndListen) {
  // Register listener.
  a11y_semantics_event_manager_->Register(listener_->GetWeakPtr());

  // Generate event.
  a11y::SemanticsEventInfo event = {.event_type = a11y::SemanticsEventType::kSemanticTreeUpdated};

  // Push event to manager.
  a11y_semantics_event_manager_->OnEvent(event);

  // Verify that listener received event.
  const auto received_events = listener_->GetReceivedEvents();
  EXPECT_EQ(received_events.size(), 1u);
  EXPECT_EQ(received_events[0].event_type, a11y::SemanticsEventType::kSemanticTreeUpdated);
}

TEST_F(A11ySemanticsEventManagerTest, ListenerGoesOutOfScope) {
  // Register listener.
  a11y_semantics_event_manager_->Register(listener_->GetWeakPtr());

  // Register scoped listener.
  {
    auto scoped_listener = std::make_unique<MockSemanticsEventListener>();
    a11y_semantics_event_manager_->Register(scoped_listener->GetWeakPtr());
  }

  // Generate event.
  a11y::SemanticsEventInfo event = {.event_type = a11y::SemanticsEventType::kSemanticTreeUpdated};

  // Push event to manager.
  a11y_semantics_event_manager_->OnEvent(event);

  // If the semantics event manager failed to unregister the scoped listener,
  // then this test would have crashed on the OnEvent() call.

  // Verify that listener_ received event.
  const auto received_events = listener_->GetReceivedEvents();
  EXPECT_EQ(received_events.size(), 1u);
  EXPECT_EQ(received_events[0].event_type, a11y::SemanticsEventType::kSemanticTreeUpdated);
}

TEST_F(A11ySemanticsEventManagerTest, SameListenerRegisteredTwice) {
  // Register listener.
  a11y_semantics_event_manager_->Register(listener_->GetWeakPtr());
  // Second registration should be a no-op.
  a11y_semantics_event_manager_->Register(listener_->GetWeakPtr());

  // Generate event.
  a11y::SemanticsEventInfo event = {.event_type = a11y::SemanticsEventType::kSemanticTreeUpdated};

  // Push event to manager.
  a11y_semantics_event_manager_->OnEvent(event);

  // Verify that listener received event.
  // If the re-registration was handled incorrectly, the listener would have
  // received the event twice.
  const auto received_events = listener_->GetReceivedEvents();
  EXPECT_EQ(received_events.size(), 1u);
  EXPECT_EQ(received_events[0].event_type, a11y::SemanticsEventType::kSemanticTreeUpdated);
}

}  // namespace accessibility_test

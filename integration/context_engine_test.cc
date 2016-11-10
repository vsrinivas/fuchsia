// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/maxwell/services/context_engine.fidl.h"
#include "apps/maxwell/services/formatting.h"
#include "lib/fidl/cpp/bindings/binding.h"

#include "apps/maxwell/acquirers/mock/mock_gps.h"
#include "apps/maxwell/integration/context_engine_test_base.h"

using namespace maxwell;
using namespace maxwell::acquirers;
using namespace maxwell::context_engine;
using namespace fidl;

namespace {

class TestListener : public ContextSubscriberLink {
 public:
  TestListener() : binding_(this) {}

  void OnUpdate(ContextUpdatePtr update) override {
    FTL_LOG(INFO) << "OnUpdate(" << update << ")";
    last_update_ = std::move(update);
  }

  void WaitForUpdate() { binding_.WaitForIncomingMethodCall(kSignalDeadline); }

  ContextUpdatePtr PopLast() { return std::move(last_update_); }

  // Binds and passes a handle that can be used to subscribe this listener.
  InterfaceHandle<ContextSubscriberLink> PassBoundHandle() {
    InterfaceHandle<ContextSubscriberLink> handle;
    binding_.Bind(GetProxy(&handle));
    return handle;
  }

 private:
  ContextUpdatePtr last_update_;
  Binding<ContextSubscriberLink> binding_;
};

class ContextEngineTest : public ContextEngineTestBase {
 public:
  ContextEngineTest() {
    cx_->RegisterSuggestionAgent("ContextEngineTest", GetProxy(&out_));
  }

 protected:
  SuggestionAgentClientPtr out_;
};

}  // namespace

TEST_F(ContextEngineTest, DirectSubscription) {
  MockGps gps(cx_);
  {
    TestListener listener;
    out_->Subscribe(MockGps::kLabel, MockGps::kSchema,
                    listener.PassBoundHandle());
    ASYNC_CHECK(gps.has_subscribers());
  }
  ASYNC_CHECK(!gps.has_subscribers());
}

TEST_F(ContextEngineTest, NoSpontaneousTransitiveSubscription) {
  MockGps gps(cx_);
  StartContextAgent("file:///system/apps/agents/carmen_sandiego");
  Sleep();
  ASYNC_CHECK(!gps.has_subscribers());
}

TEST_F(ContextEngineTest, TransitiveSubscription) {
  MockGps gps(cx_);
  StartContextAgent("file:///system/apps/agents/carmen_sandiego");
  {
    TestListener listener;
    out_->Subscribe("/location/region", "json:string",
                    listener.PassBoundHandle());
    ASYNC_CHECK(gps.has_subscribers());

    gps.Publish(90, 0);
    listener.WaitForUpdate();
    ContextUpdatePtr update = listener.PopLast();
    EXPECT_EQ("file:///system/apps/agents/carmen_sandiego", update->source);
    EXPECT_EQ("\"The Arctic\"", update->json_value);

    gps.Publish(-90, 0);
    listener.WaitForUpdate();
    update = listener.PopLast();
    EXPECT_EQ("\"Antarctica\"", update->json_value);
  }
  ASYNC_CHECK(!gps.has_subscribers());
}

TEST_F(ContextEngineTest, PublishAfterSubscribe) {
  TestListener listener;
  out_->Subscribe(MockGps::kLabel, MockGps::kSchema,
                  listener.PassBoundHandle());
  Sleep();

  MockGps gps(cx_);
  ASYNC_CHECK(gps.has_subscribers());

  gps.Publish(90, 0);
  listener.WaitForUpdate();
  EXPECT_TRUE(listener.PopLast());
}

TEST_F(ContextEngineTest, SubscribeAfterPublish) {
  MockGps gps(cx_);
  gps.Publish(90, 0);
  Sleep();

  TestListener listener;
  out_->Subscribe(MockGps::kLabel, MockGps::kSchema,
                  listener.PassBoundHandle());
  listener.WaitForUpdate();
  EXPECT_TRUE(listener.PopLast());
}

TEST_F(ContextEngineTest, MultipleSubscribers) {
  MockGps gps(cx_);
  TestListener listeners[2];
  for (auto& listener : listeners)
    out_->Subscribe(MockGps::kLabel, MockGps::kSchema,
                    listener.PassBoundHandle());

  gps.Publish(90, 0);
  for (auto& listener : listeners) {
    listener.WaitForUpdate();
    EXPECT_TRUE(listener.PopLast());
  }
}

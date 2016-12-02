// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/maxwell/lib/context/formatting.h"
#include "apps/maxwell/services/context/context_engine.fidl.h"
#include "apps/maxwell/src/acquirers/mock/mock_gps.h"
#include "apps/maxwell/src/integration/context_engine_test_base.h"
#include "lib/fidl/cpp/bindings/binding.h"

namespace {

class TestListener : public maxwell::ContextSubscriberLink {
 public:
  TestListener() : binding_(this) {}

  void OnUpdate(maxwell::ContextUpdatePtr update) override {
    FTL_LOG(INFO) << "OnUpdate(" << update << ")";
    last_update_ = std::move(update);
  }

  void WaitForUpdate() { binding_.WaitForIncomingMethodCall(kSignalDeadline); }

  maxwell::ContextUpdatePtr PopLast() { return std::move(last_update_); }

  // Binds and passes a handle that can be used to subscribe this listener.
  fidl::InterfaceHandle<maxwell::ContextSubscriberLink> PassBoundHandle() {
    fidl::InterfaceHandle<maxwell::ContextSubscriberLink> handle;
    binding_.Bind(handle.NewRequest());
    return handle;
  }

 private:
  maxwell::ContextUpdatePtr last_update_;
  fidl::Binding<maxwell::ContextSubscriberLink> binding_;
};

class ContextEngineTest : public ContextEngineTestBase {
 public:
  ContextEngineTest() {
    context_engine()->RegisterSubscriber("ContextEngineTest",
                                         out_.NewRequest());
  }

 protected:
  maxwell::ContextSubscriberPtr out_;
};

}  // namespace

TEST_F(ContextEngineTest, DirectSubscription) {
  maxwell::acquirers::MockGps gps(context_engine());
  {
    TestListener listener;
    out_->Subscribe(maxwell::acquirers::MockGps::kLabel,
                    maxwell::acquirers::MockGps::kSchema,
                    listener.PassBoundHandle());
    ASYNC_CHECK(gps.has_subscribers());
  }
  ASYNC_CHECK(!gps.has_subscribers());
}

TEST_F(ContextEngineTest, NoSpontaneousTransitiveSubscription) {
  maxwell::acquirers::MockGps gps(context_engine());
  StartContextAgent("file:///system/apps/agents/carmen_sandiego");
  Sleep();
  ASYNC_CHECK(!gps.has_subscribers());
}

TEST_F(ContextEngineTest, TransitiveSubscription) {
  maxwell::acquirers::MockGps gps(context_engine());
  StartContextAgent("file:///system/apps/agents/carmen_sandiego");
  {
    TestListener listener;
    out_->Subscribe("/location/region", "json:string",
                    listener.PassBoundHandle());
    ASYNC_CHECK(gps.has_subscribers());

    gps.Publish(90, 0);
    listener.WaitForUpdate();
    maxwell::ContextUpdatePtr update = listener.PopLast();
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
  out_->Subscribe(maxwell::acquirers::MockGps::kLabel,
                  maxwell::acquirers::MockGps::kSchema,
                  listener.PassBoundHandle());
  Sleep();

  maxwell::acquirers::MockGps gps(context_engine());
  ASYNC_CHECK(gps.has_subscribers());

  gps.Publish(90, 0);
  listener.WaitForUpdate();
  EXPECT_TRUE(listener.PopLast());
}

TEST_F(ContextEngineTest, SubscribeAfterPublish) {
  maxwell::acquirers::MockGps gps(context_engine());
  gps.Publish(90, 0);
  Sleep();

  TestListener listener;
  out_->Subscribe(maxwell::acquirers::MockGps::kLabel,
                  maxwell::acquirers::MockGps::kSchema,
                  listener.PassBoundHandle());
  listener.WaitForUpdate();
  EXPECT_TRUE(listener.PopLast());
}

TEST_F(ContextEngineTest, MultipleSubscribers) {
  maxwell::acquirers::MockGps gps(context_engine());
  TestListener listeners[2];
  for (auto& listener : listeners)
    out_->Subscribe(maxwell::acquirers::MockGps::kLabel,
                    maxwell::acquirers::MockGps::kSchema,
                    listener.PassBoundHandle());

  gps.Publish(90, 0);
  for (auto& listener : listeners) {
    listener.WaitForUpdate();
    EXPECT_TRUE(listener.PopLast());
  }
}

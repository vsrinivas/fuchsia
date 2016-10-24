// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/maxwell/integration/test.h"

#include "apps/maxwell/interfaces/context_engine.mojom.h"
#include "apps/maxwell/interfaces/formatting.h"
#include "mojo/public/cpp/application/application_test_base.h"
#include "mojo/public/cpp/application/connect.h"
#include "mojo/public/cpp/bindings/binding.h"

#include "apps/maxwell/acquirers/mock/mock_gps.h"

using namespace maxwell;
using namespace maxwell::acquirers;
using namespace maxwell::context_engine;
using namespace mojo;

namespace {

class TestListener : public ContextSubscriberLink {
 public:
  TestListener() : binding_(this) {}

  void OnUpdate(ContextUpdatePtr update) override {
    MOJO_LOG(INFO) << "OnUpdate(" << update << ")";
    last_update_ = std::move(update);
  }

  void WaitForUpdate() { binding_.WaitForIncomingMethodCall(kMojoDeadline); }

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

class ContextEngineTest : public DebuggableAppTestBase {
 protected:
  void SetUp() override {
    DebuggableAppTestBase::SetUp();
    cx_debug_ = ConnectToDebuggableService(shell(), "mojo:context_engine",
                                           GetProxy(&cx_));
  }

  void KillAllDependencies() override {
    DebuggableAppTestBase::KillAllDependencies();
    // TODO(rosswang): Remove this after app_mgr v2.
    // Since agents and acquirers have an intrinsic dependency on Context
    // Engine, if we were to start it normally in SetUp and use the default
    // parallel kill, it would come back to haunt us, so we need to kill it
    // again.
    ConnectToService("mojo:context_engine", GetProxy(&cx_debug_));
    cx_debug_->Kill();  // with a shotgun
    WAIT_UNTIL(cx_debug_.encountered_error());
    // cx_debug_.is_bound() (implicit bool) remains true
  }

  SuggestionAgentClientPtr cx_;

 private:
  DebugPtr cx_debug_;
};

}  // namespace

TEST_F(ContextEngineTest, NoSpontaneousSubscription) {
  MockGps gps(shell());
  StartComponent("mojo:agents/carmen_sandiego");

  ASYNC_CHECK(!gps.has_subscribers());
}

TEST_F(ContextEngineTest, Subscription) {
  MockGps gps(shell());
  StartComponent("mojo:agents/carmen_sandiego");
  {
    TestListener listener;
    cx_->Subscribe("/location/region", "json:string",
                   listener.PassBoundHandle());
    ASYNC_CHECK(gps.has_subscribers());

    gps.Publish(90, 0);
    listener.WaitForUpdate();
    ContextUpdatePtr update = listener.PopLast();
    EXPECT_EQ("mojo:agents/carmen_sandiego", update->source);
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
  cx_->Subscribe(MockGps::kLabel, MockGps::kSchema, listener.PassBoundHandle());
  Sleep();

  MockGps gps(shell());
  ASYNC_CHECK(gps.has_subscribers());

  gps.Publish(90, 0);
  listener.WaitForUpdate();
  EXPECT_TRUE(listener.PopLast());
}

TEST_F(ContextEngineTest, SubscribeAfterPublish) {
  MockGps gps(shell());
  gps.Publish(90, 0);
  Sleep();

  TestListener listener;
  cx_->Subscribe(MockGps::kLabel, MockGps::kSchema, listener.PassBoundHandle());
  listener.WaitForUpdate();
  EXPECT_TRUE(listener.PopLast());
}

TEST_F(ContextEngineTest, MultipleSubscribers) {
  MockGps gps(shell());
  TestListener listeners[2];
  for (auto& listener : listeners)
    cx_->Subscribe(MockGps::kLabel, MockGps::kSchema,
                   listener.PassBoundHandle());

  gps.Publish(90, 0);
  for (auto& listener : listeners) {
    listener.WaitForUpdate();
    EXPECT_TRUE(listener.PopLast());
  }
}

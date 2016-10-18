// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/maxwell/integration/test.h"

#include "apps/maxwell/interfaces/suggestion_manager.mojom.h"
#include "apps/maxwell/interfaces/formatting.h"
#include "mojo/public/cpp/application/application_test_base.h"
#include "mojo/public/cpp/application/connect.h"
#include "mojo/public/cpp/bindings/binding.h"

#include "apps/maxwell/acquirers/mock/mock_gps.h"

using namespace maxwell::acquirers;
using namespace maxwell::suggestion_engine;
using namespace mojo;

class TestListener : public SuggestionListener {
 public:
  void OnAdd(mojo::Array<SuggestionPtr> suggestions) override {
    MOJO_LOG(INFO) << "OnAdd(" << suggestions << ")";
    suggestion_count_ += suggestions.size();
  }

  void OnRemove(const mojo::String& uuid) override {
    MOJO_LOG(INFO) << "OnRemove(" << uuid << ")";
    suggestion_count_--;
  }

  void OnRemoveAll() override {
    MOJO_LOG(INFO) << "OnRemoveAll";
    suggestion_count_ = 0;
  }

  int suggestion_count() const { return suggestion_count_; }

 private:
  int suggestion_count_ = 0;
};

class ResultCountTest : public mojo::test::ApplicationTestBase {
 public:
  ResultCountTest() : listener_binding_(&listener_) {}

 protected:
  void SetUp() override {
    ApplicationTestBase::SetUp();
    // shell is only available after base class SetUp; it is not available at
    // construction time.
    // TODO(rosswang): See if Mojo is open to initializing shell in the
    // constructor.
    gps_ = std::unique_ptr<MockGps>(new MockGps(shell()));

    StartComponent(shell(), "mojo:agents/carmen_sandiego");
    StartComponent(shell(), "mojo:agents/ideas");
    ConnectToService(shell(), "mojo:suggestion_engine",
                     GetProxy(&suggestion_manager_));

    SuggestionListenerPtr lp;
    listener_binding_.Bind(GetProxy(&lp));
    suggestion_manager_->SubscribeToNext(lp.PassInterfaceHandle(),
                                         GetProxy(&ctl_));
  }

  void SetResultCount(int count) { ctl_->SetResultCount(count); }

  void PublishNewSignal() {
    // TODO(rosswang): After Suggestion Engine dedups by id, change this.
    // TODO(rosswang): Populate additional suggestions through a legit channel.
    gps_->Publish(90, 0);
  }

  void CheckResultCount(int expected) {
    ASYNC_CHECK(listener_.suggestion_count() == expected);
  }

 private:
  std::unique_ptr<MockGps> gps_;
  SuggestionManagerPtr suggestion_manager_;
  TestListener listener_;
  Binding<SuggestionListener> listener_binding_;
  NextControllerPtr ctl_;
};

TEST_F(ResultCountTest, SetResultCount) {
  SetResultCount(3);
  CheckResultCount(0);

  PublishNewSignal();
  CheckResultCount(1);
  // Note that without the above ASYNC_CHECK or a pause, this context update may
  // be lost due to the subscription not yet having happened.

  PublishNewSignal();
  CheckResultCount(2);

  SetResultCount(0);
  CheckResultCount(0);

  SetResultCount(3);
  CheckResultCount(2);

  PublishNewSignal();
  CheckResultCount(3);
  PublishNewSignal();  // available = 4
  CheckResultCount(3);
  SetResultCount(3);
  CheckResultCount(3);

  SetResultCount(4);
  CheckResultCount(4);

  SetResultCount(10);
  CheckResultCount(4);
  PublishNewSignal();
  PublishNewSignal();  // available = 6
  CheckResultCount(6);

  SetResultCount(1);
  CheckResultCount(1);
}

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

class ResultCountTest : public DebuggableAppTestBase {
 public:
  ResultCountTest() : listener_binding_(&listener_) {}

 protected:
  void SetUp() override {
    DebuggableAppTestBase::SetUp();
    // shell is only available after base class SetUp; it is not available at
    // construction time.
    // TODO(rosswang): See if Mojo is open to initializing shell in the
    // constructor.
    gps_ = std::unique_ptr<MockGps>(new MockGps(shell()));

    StartComponent("mojo:context_engine");
    StartComponent("mojo:agents/carmen_sandiego");
    StartComponent("mojo:agents/ideas");
    ConnectToService("mojo:suggestion_engine", GetProxy(&suggestion_manager_));

    SuggestionListenerPtr lp;
    listener_binding_.Bind(GetProxy(&lp));
    suggestion_manager_->SubscribeToNext(lp.PassInterfaceHandle(),
                                         GetProxy(&ctl_));
    Sleep(); // Give transitive links a chance to form before publishing.
  }

  void SetResultCount(int count) { ctl_->SetResultCount(count); }

  // Publishes n new signals to context.
  void PublishNewSignal(int n = 1) {
    // TODO(rosswang): After Suggestion Engine dedups by id, change this.
    // TODO(rosswang): Populate additional suggestions through a legit channel.
    for (int i = 0; i < n; i++)
      gps_->Publish(90, 0);
  }

  int suggestion_count() const { return listener_.suggestion_count(); }

 private:
  std::unique_ptr<MockGps> gps_;
  SuggestionManagerPtr suggestion_manager_;
  TestListener listener_;
  Binding<SuggestionListener> listener_binding_;
  NextControllerPtr ctl_;
};

// Macro rather than method to capture the expectation in the assertion message.
#define CHECK_RESULT_COUNT(expected) ASYNC_CHECK(suggestion_count() == expected)

TEST_F(ResultCountTest, InitiallyEmpty) {
  SetResultCount(10);
  CHECK_RESULT_COUNT(0);
}

TEST_F(ResultCountTest, OneByOne) {
  SetResultCount(10);
  PublishNewSignal();
  CHECK_RESULT_COUNT(1);

  PublishNewSignal();
  CHECK_RESULT_COUNT(2);
}

TEST_F(ResultCountTest, AddOverLimit) {
  PublishNewSignal(3);
  CHECK_RESULT_COUNT(0);

  SetResultCount(1);
  CHECK_RESULT_COUNT(1);

  SetResultCount(3);
  CHECK_RESULT_COUNT(3);

  SetResultCount(5);
  CHECK_RESULT_COUNT(3);

  PublishNewSignal(4);
  CHECK_RESULT_COUNT(5);
}

TEST_F(ResultCountTest, Clear) {
  SetResultCount(10);
  PublishNewSignal(3);
  CHECK_RESULT_COUNT(3);

  SetResultCount(0);
  CHECK_RESULT_COUNT(0);

  SetResultCount(10);
  CHECK_RESULT_COUNT(3);
}

TEST_F(ResultCountTest, MultiRemove) {
  SetResultCount(10);
  PublishNewSignal(3);
  CHECK_RESULT_COUNT(3);

  SetResultCount(1);
  CHECK_RESULT_COUNT(1);

  SetResultCount(10);
  CHECK_RESULT_COUNT(3);
}

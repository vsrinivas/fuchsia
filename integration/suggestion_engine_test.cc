// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/maxwell/integration/test.h"

#include "apps/maxwell/interfaces/suggestion_manager.mojom.h"
#include "apps/maxwell/interfaces/formatting.h"
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

void TestSuggestionEngine(Shell* shell) {
  MockGps gps(shell);
  StartComponent(shell, "mojo:agents/carmen_sandiego");
  StartComponent(shell, "mojo:agents/ideas");

  SuggestionManagerPtr s;
  ConnectToService(shell, "mojo:suggestion_engine", GetProxy(&s));

  TestListener listener;
  SuggestionListenerPtr lp;
  Binding<SuggestionListener> listener_binding(&listener, GetProxy(&lp));
  NextControllerPtr ctl;

  s->SubscribeToNext(lp.PassInterfaceHandle(), GetProxy(&ctl));
  ctl->SetResultCount(3);
  ASYNC_CHECK(listener.suggestion_count() == 0);

  gps.Publish(90, 0);
  ASYNC_CHECK(listener.suggestion_count() == 1);
  // Note that without the above ASYNC_CHECK or a pause, this context update may
  // be lost due to the subscription not yet having happened.

  gps.Publish(-90, 0);
  // TODO(rosswang): After Suggestion Engine dedups by id, change this.
  ASYNC_CHECK(listener.suggestion_count() == 2);

  ctl->SetResultCount(0);
  ASYNC_CHECK(listener.suggestion_count() == 0);

  ctl->SetResultCount(3);
  ASYNC_CHECK(listener.suggestion_count() == 2);

  // TODO(rosswang): Populate additional suggestions through a legit channel.
  gps.Publish(90, 0);
  ASYNC_CHECK(listener.suggestion_count() == 3);
  gps.Publish(-90, 0);  // available = 4
  ASYNC_CHECK(listener.suggestion_count() == 3);
  ctl->SetResultCount(3);
  ASYNC_CHECK(listener.suggestion_count() == 3);

  ctl->SetResultCount(4);
  ASYNC_CHECK(listener.suggestion_count() == 4);

  ctl->SetResultCount(10);
  ASYNC_CHECK(listener.suggestion_count() == 4);
  gps.Publish(90, 0);
  gps.Publish(-90, 0);  // available = 6
  ASYNC_CHECK(listener.suggestion_count() == 6);

  ctl->SetResultCount(1);
  ASYNC_CHECK(listener.suggestion_count() == 1);
}

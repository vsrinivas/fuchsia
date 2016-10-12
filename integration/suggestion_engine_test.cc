// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/maxwell/test.h"

#include "apps/maxwell/interfaces/suggestion_manager.mojom.h"
#include "apps/maxwell/interfaces/formatting.h"
#include "mojo/public/cpp/application/connect.h"
#include "mojo/public/cpp/bindings/binding.h"

using namespace maxwell::suggestion_engine;
using namespace mojo;

class TestListener : public SuggestionListener {
 public:
  void OnAdd(mojo::Array<SuggestionPtr> suggestions) override {
    MOJO_LOG(INFO) << "OnAdd(" << suggestions << ")";
  }

  void OnRemove(const mojo::String& uuid) override {
    MOJO_LOG(INFO) << "OnRemove(\"" << uuid << "\")";
  }

  void OnRemoveAll() override { MOJO_LOG(INFO) << "OnRemoveAll"; }
};

void TestSuggestionEngine(Shell* shell) {
  StartComponent(shell, "mojo:acquirers/gps");
  StartComponent(shell, "mojo:agents/carmen_sandiego");
  StartComponent(shell, "mojo:agents/ideas");

  SuggestionManagerPtr s;
  ConnectToService(shell, "mojo:suggestion_engine", GetProxy(&s));

  TestListener listener_impl;
  SuggestionListenerPtr lp;
  Binding<SuggestionListener> listener(&listener_impl, GetProxy(&lp));
  NextControllerPtr ctl;

  s->SubscribeToNext(lp.PassInterfaceHandle(), GetProxy(&ctl));
  ctl->SetResultCount(3);
}

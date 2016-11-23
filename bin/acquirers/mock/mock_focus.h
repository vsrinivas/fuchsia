// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "apps/maxwell/services/context/context_engine.fidl.h"

#include "apps/maxwell/src/acquirers/focus.h"
#include "apps/modular/services/user/focus.fidl.h"
#include "lib/fidl/cpp/bindings/binding.h"
#include "lib/fidl/cpp/bindings/array.h"

namespace maxwell {
namespace acquirers {

class MockFocusAcquirer : public FocusAcquirer,
                          public context::PublisherController {
 public:
  MockFocusAcquirer(context::ContextEngine* context_engine);

  // Publishes whether |ids| is empty.
  template <class Collection>
  void OnFocusChanged(Collection& ids) {
    if (ids.size() == 0) {
      Publish(0);
    } else {
      Publish(1);
    }
  }
  void Publish(int modular_state);
  void OnHasSubscribers() override;
  void OnNoSubscribers() override;

  bool has_subscribers() const { return has_subscribers_; }

 private:
  fidl::Binding<context::PublisherController> ctl_;
  context::PublisherLinkPtr out_;
  bool has_subscribers_ = false;
};

}  // namespace acquirers
}  // namespace maxwell

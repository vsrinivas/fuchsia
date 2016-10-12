// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "apps/maxwell/interfaces/context_engine.mojom.h"
#include "mojo/public/interfaces/application/shell.mojom.h"

#include "apps/maxwell/acquirers/gps.h"
#include "mojo/public/cpp/bindings/binding.h"

namespace maxwell {
namespace acquirers {

class MockGps : public GpsAcquirer,
                public context_engine::ContextPublisherController {
 public:
  MockGps(mojo::Shell* shell);
  void Publish(float latitude, float longitude);

  // Test harnesses may override this as appropriate.
  void OnHasSubscribers() override {}
  // Test harnesses may override this as appropriate.
  void OnNoSubscribers() override {}

 private:
  mojo::Binding<context_engine::ContextPublisherController> ctl_;
  context_engine::ContextPublisherLinkPtr out_;
};

}  // namespace acquirers
}  // namespace maxwell

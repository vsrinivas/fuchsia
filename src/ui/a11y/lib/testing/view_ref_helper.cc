// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/testing/view_ref_helper.h"

#include <lib/syslog/cpp/logger.h>
#include <zircon/status.h>
#include <zircon/types.h>

#include "src/ui/a11y/lib/util/util.h"

namespace accessibility_test {

ViewRefHelper::ViewRefHelper() {
  FX_CHECK(zx::eventpair::create(0u, &view_ref_.reference, &eventpair_peer_) == ZX_OK);
}

void ViewRefHelper::SendEventPairSignal() { eventpair_peer_.reset(); }

fuchsia::ui::views::ViewRef ViewRefHelper::Clone() const { return a11y::Clone(view_ref_); }

zx_koid_t ViewRefHelper::koid() const { return a11y::GetKoid(view_ref_); }

}  // namespace accessibility_test

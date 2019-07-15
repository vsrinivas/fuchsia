// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/sessionmgr/story_runner/ongoing_activity_impl.h"

#include <fuchsia/modular/cpp/fidl.h>

namespace modular {

using fuchsia::modular::OngoingActivityType;

OngoingActivityImpl::OngoingActivityImpl(OngoingActivityType ongoing_activity_type,
                                         fit::closure on_destroy)
    : ongoing_activity_type_(ongoing_activity_type), on_destroy_(std::move(on_destroy)) {}

OngoingActivityImpl::~OngoingActivityImpl() { on_destroy_(); }

OngoingActivityType OngoingActivityImpl::GetType() { return ongoing_activity_type_; }

}  // namespace modular

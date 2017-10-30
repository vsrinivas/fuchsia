// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/suggestion_engine/suggestion_subscriber.h"

namespace maxwell {

SuggestionSubscriber::SuggestionSubscriber(
    fidl::InterfaceHandle<SuggestionListener> listener)
    : listener_(SuggestionListenerPtr::Create(std::move(listener))) {}

SuggestionSubscriber::~SuggestionSubscriber() = default;

}  // namespace maxwell

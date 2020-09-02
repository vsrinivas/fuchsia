// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "settable_time_source.h"

#include "third_party/roughtime/protocol.h"
#include "third_party/roughtime/time_source.h"

namespace time_server {

// Uncertainty radius for current time.
static constexpr unsigned int kUncertaintyMicros = 5'000'000;

SettableTimeSource::SettableTimeSource() : SettableTimeSource(0) {}

SettableTimeSource::SettableTimeSource(roughtime::rough_time_t initial_time_micros)
    : now_micros_(initial_time_micros) {}
SettableTimeSource::SettableTimeSource(SettableTimeSource&& rhs) noexcept = default;

SettableTimeSource& SettableTimeSource::operator=(time_server::SettableTimeSource const& rhs) =
    default;

SettableTimeSource::~SettableTimeSource() = default;

void SettableTimeSource::SetTime(roughtime::rough_time_t now_micros) { now_micros_ = now_micros; }

std::pair<roughtime::rough_time_t, uint32_t> SettableTimeSource::Now() {
  return std::make_pair(now_micros_, kUncertaintyMicros);
}

}  // namespace time_server

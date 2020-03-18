// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_SCHEDULING_ID_H_
#define SRC_UI_SCENIC_LIB_SCHEDULING_ID_H_

#include <cstdint>
#include <functional>

namespace {

#define SESSION_TRACE_ID(session_id, present_id) \
  (((uint64_t)(session_id) << 32) | ((present_id)&0xFFFFFFFF))

}  // anonymous namespace

namespace scheduling {

// ID used to schedule an update on a FrameScheduler client. Each client is assumed to have a
// globally and temporally unique SessionId.
using SessionId = uint64_t;

// ID used to the schedule a present update within a Session. PresentIds are globally unique.
using PresentId = uint64_t;

// Value 0 reserved as invalid.
constexpr scheduling::SessionId kInvalidSessionId = 0u;
constexpr scheduling::PresentId kInvalidPresentId = 0u;

// These methods are necessary to maintain id consistency between frame schedulers as sessions
// switch between them. Generates a new global id. Thread-safe.
SessionId GetNextSessionId();
// Generates a new global id. Thread-safe.
PresentId GetNextPresentId();

// Id pair for Present call identification.
struct SchedulingIdPair {
  SessionId session_id;
  PresentId present_id;

  bool operator==(const SchedulingIdPair& rhs) const {
    return session_id == rhs.session_id && present_id == rhs.present_id;
  }
  bool operator<(const SchedulingIdPair& rhs) const {
    return session_id != rhs.session_id ? session_id < rhs.session_id : present_id < rhs.present_id;
  }
};

}  // namespace scheduling

namespace std {

// A hash specialization for SchedulingIdPair, so that they can be stored in maps.
template <>
struct hash<scheduling::SchedulingIdPair> {
  size_t operator()(const scheduling::SchedulingIdPair& pair) const noexcept {
    return (pair.session_id << 32) | (pair.present_id & 0x00000000FFFFFFFF);
  }
};

}  // namespace std

#endif  // SRC_UI_SCENIC_LIB_SCHEDULING_ID_H_

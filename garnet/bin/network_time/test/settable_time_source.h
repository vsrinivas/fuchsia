// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_NETWORK_TIME_TEST_SETTABLE_TIME_SOURCE_H_
#define GARNET_BIN_NETWORK_TIME_TEST_SETTABLE_TIME_SOURCE_H_

#include "third_party/roughtime/protocol.h"
#include "third_party/roughtime/time_source.h"

namespace time_server {

// A |TimeSource| implementation whose current time can be set using |SetTime|.
// (Note: Time does not advance automatically.)
//
// This class is used to provide the time for a local Roughtime server.
class SettableTimeSource : public roughtime::TimeSource {
 public:
  SettableTimeSource();
  explicit SettableTimeSource(roughtime::rough_time_t initial_time_micros);

  SettableTimeSource(SettableTimeSource&& rhs) noexcept;
  SettableTimeSource& operator=(SettableTimeSource const& rhs);

  ~SettableTimeSource() override;

  // Set the current time
  void SetTime(roughtime::rough_time_t now_micros);
  std::pair<roughtime::rough_time_t, uint32_t> Now() override;

 private:
  // Current time in epoch microseconds.
  // TODO(kpozin): Use std::atomic? Would require custom copy & move ctors.
  roughtime::rough_time_t now_micros_ = 0;
};

}  // namespace time_server

#endif  // GARNET_BIN_NETWORK_TIME_TEST_SETTABLE_TIME_SOURCE_H_

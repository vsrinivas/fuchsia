// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/media/tools/flog_viewer/formatting.h"

#include <iomanip>
#include <iostream>

#include "apps/media/services/flog/flog.fidl.h"

namespace flog {
namespace {

static constexpr int64_t kNanosecondsPerSecond = 1000000000ll;
static constexpr int64_t kSecondsPerMinute = 60ll;
static constexpr int64_t kMinutesPerHour = 60ll;

}  // namespace

int ostream_indent_index() {
  static int i = std::ios_base::xalloc();
  return i;
}

std::ostream& begl(std::ostream& os) {
  for (long i = 0; i < os.iword(ostream_indent_index()); i++) {
    os << "    ";
  }
  return os;
}

std::ostream& operator<<(std::ostream& os, const EntryHeader& value) {
  if (value.entry_.is_null()) {
    return os << "NULL ENTRY";
  }

  // Print <log_id>.<index> <hh:mm:ss>.<nanoseconds> <log_id>.<channel_id>
  return os << value.entry_->log_id << "." << std::setfill('0') << std::setw(6)
            << value.index_ << " " << AsNiceDateTime(value.entry_->time_ns)
            << "." << std::setfill('0') << std::setw(9)
            << value.entry_->time_ns % kNanosecondsPerSecond << " "
            << value.entry_->log_id << "." << std::setw(2) << std::setfill('0')
            << value.entry_->channel_id << " ";
}

std::ostream& operator<<(std::ostream& os, AsAddress value) {
  if (value.address_ == 0) {
    return os << "nullptr";
  }

  return os << "0x" << std::hex << std::setw(16) << std::setfill('0')
            << value.address_ << std::dec;
}

std::ostream& operator<<(std::ostream& os, AsKoid value) {
  if (value.koid_ == 0) {
    return os << "nullptr";
  }

  return os << "0x" << std::hex << std::setw(16) << std::setfill('0')
            << value.koid_ << std::dec;
}

std::ostream& operator<<(std::ostream& os, AsNiceDateTime value) {
  int64_t seconds = value.time_ns_ / kNanosecondsPerSecond;
  int64_t minutes = seconds / kSecondsPerMinute;
  int64_t hours = minutes / kMinutesPerHour;
  seconds %= kSecondsPerMinute;
  minutes %= kMinutesPerHour;

  // Our timestamps are relative to startup, so no point in showing the date.
  return os << std::setfill('0') << std::setw(2) << hours << ":" << std::setw(2)
            << minutes << ":" << std::setw(2) << seconds;
}

std::ostream& operator<<(std::ostream& os, const Channel& value) {
  if (!value.resolved()) {
    return os << "unresolved address " << AsAddress(value.subject_address());
  }

  return os << "CHANNEL " << value.log_id() << "." << std::setw(2)
            << std::setfill('0') << value.channel_id();
}

std::ostream& operator<<(std::ostream& os, const ChildBinding& value) {
  std::shared_ptr<Channel> channel = value.channel();

  if (channel) {
    os << *channel << " ";
    channel->PrintAccumulator(os);
    return os;
  }

  if (value.koid() == 0) {
    return os << "<none>";
  }

  return os << "unresolved binding, koid " << AsKoid(value.koid());
}

std::ostream& operator<<(std::ostream& os, const PeerBinding& value) {
  std::shared_ptr<Channel> channel = value.channel();

  if (channel) {
    return os << *channel;
  }

  if (value.koid() == 0) {
    return os << "<none>";
  }

  return os << "unresolved binding, koid " << AsKoid(value.koid());
}

}  // namespace flog

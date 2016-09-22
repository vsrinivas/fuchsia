// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/media/tools/flog_viewer/formatting.h"

#include <chrono>
#include <iomanip>
#include <iostream>

#include "apps/media/interfaces/flog/flog.mojom.h"

namespace mojo {
namespace flog {

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

std::ostream& operator<<(std::ostream& os, AsAddress value) {
  if (value.address_ == 0) {
    return os << "nullptr";
  }

  return os << "0x" << std::hex << std::setw(8) << std::setfill('0')
            << value.address_ << std::dec;
}

std::ostream& operator<<(std::ostream& os, AsNiceDateTime value) {
  std::time_t time = std::chrono::system_clock::to_time_t(
      std::chrono::time_point<std::chrono::system_clock>(
          std::chrono::microseconds(value.time_us_)));
  std::tm* tm = localtime(&time);
  return os << std::setw(4) << tm->tm_year + 1900 << "/" << std::setw(2)
            << std::setfill('0') << tm->tm_mon + 1 << "/" << std::setw(2)
            << tm->tm_mday << " " << std::setw(2) << tm->tm_hour << ":"
            << std::setw(2) << tm->tm_min << ":" << std::setw(2) << tm->tm_sec;
}

std::ostream& operator<<(std::ostream& os, AsMicroseconds value) {
  return os << std::setfill('0') << std::setw(6) << value.time_us_ % 1000000ull;
}

std::ostream& operator<<(std::ostream& os, const Channel& value) {
  if (!value.resolved()) {
    return os << "unresolved address " << AsAddress(value.subject_address());
  }

  return os << "CHANNEL " << value.log_id() << "." << std::setw(2)
            << std::setfill('0') << value.channel_id();
}

int ostream_entry_second_index() {
  static int i = std::ios_base::xalloc();
  return i;
}

std::ostream& operator<<(std::ostream& os, const FlogEntryPtr& value) {
  if (value.is_null()) {
    return os << "NULL ENTRY";
  }

  // We want to know if this entry happened in a different second than the
  // previous entry. To do this, we use ostream::iword to store a time value
  // at seconds resolution. We mod it by max long so it'll fit in a long.
  long second = static_cast<long>((value->time_us / 1000000ull) %
                                  std::numeric_limits<long>::max());

  // If this second value differs from the previous one, record the new second
  // value and print a second header.
  if (os.iword(ostream_entry_second_index()) != second) {
    os.iword(ostream_entry_second_index()) = second;
    os << AsNiceDateTime(value->time_us) << std::endl;
  }

  // Print <microseconds> <log_id>.<channel_id>
  return os << AsMicroseconds(value->time_us) << " " << value->log_id << "."
            << std::setw(2) << std::setfill('0') << value->channel_id << " ";
}

}  // namespace flog
}  // namespace mojo

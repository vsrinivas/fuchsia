// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/media/tools/flog_viewer/accumulator.h"

#include <iomanip>
#include <iostream>

#include "apps/media/tools/flog_viewer/formatting.h"

namespace flog {

std::ostream& operator<<(std::ostream& os, const Accumulator::Problem& value) {
  return os << std::setfill('0') << std::setw(6) << value.entry_index() << " "
            << AsNiceDateTime(value.time_ns()) << "." << std::setfill('0')
            << std::setw(9) << value.time_ns() % 1000000000ll << " "
            << value.log_id() << "." << std::setw(2) << std::setfill('0')
            << value.channel_id() << " " << value.message();
}

Accumulator::Accumulator() {}

Accumulator::~Accumulator() {}

std::ostream& Accumulator::ReportProblem(uint32_t entry_index,
                                         const FlogEntryPtr& entry) {
  problems_.emplace_back(entry->log_id, entry->channel_id, entry->time_ns,
                         entry_index);
  return problems_.back().stream();
}

void Accumulator::PrintProblems(std::ostream& os) {
  for (const Problem& problem : problems_) {
    os << begl << "PROBLEM: " << problem << std::endl;
  }
}

void Accumulator::Print(std::ostream& os) {
  PrintProblems(os);
}

Accumulator::Problem::Problem(uint32_t log_id,
                              uint32_t channel_id,
                              int64_t time_ns,
                              uint32_t entry_index)
    : log_id_(log_id),
      channel_id_(channel_id),
      time_ns_(time_ns),
      entry_index_(entry_index) {}

Accumulator::Problem::~Problem() {}

}  // namespace flog

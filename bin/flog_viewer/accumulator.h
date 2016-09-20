// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MEDIA_TOOLS_FLOG_VIEWER_ACCUMULATOR_H_
#define APPS_MEDIA_TOOLS_FLOG_VIEWER_ACCUMULATOR_H_

#include <limits>

#include "apps/media/interfaces/flog/flog.mojom.h"
#include "mojo/public/cpp/bindings/message.h"

namespace mojo {
namespace flog {

// Base class for accumulators produced by handlers that analyze message
// streams.
//
// Some channel handlers (particularly the ones for the 'digest' format) will
// produce an accumulator, which reflects the handler's understanding of the
// messages that have been handled.
class Accumulator {
public:
  class Problem {
  public:
    Problem(uint32_t log_id, uint32_t channel_id, uint32_t entry_index);

    Problem(Problem &&other)
        : log_id_(other.log_id()), channel_id_(other.channel_id()),
          entry_index_(other.entry_index()) {
      stream_ << other.message();
    }

    ~Problem();

    uint32_t log_id() const { return log_id_; }
    uint32_t channel_id() const { return channel_id_; }
    uint32_t entry_index() const { return entry_index_; }
    std::ostream &stream() { return stream_; }
    std::string message() const { return stream_.str(); }

  private:
    uint32_t log_id_;
    uint32_t channel_id_;
    uint32_t entry_index_;
    std::ostringstream stream_;
  };

  virtual ~Accumulator();

  // Report a problem.
  std::ostream &ReportProblem(uint32_t entry_index, const FlogEntryPtr &entry);

  // Prints reported problems.
  void PrintProblems(std::ostream &os);

  // Prints the contents of the accumulator to os. The default implementation
  // calls PrintProblems.
  virtual void Print(std::ostream &os);

protected:
  Accumulator();

private:
  std::vector<Problem> problems_;
};

} // namespace flog
} // namespace mojo

#endif // APPS_MEDIA_TOOLS_FLOG_VIEWER_ACCUMULATOR_H_

// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "timer.h"
#include "trace.h"

#pragma once

namespace overnet {

inline TraceSink TraceCout(Timer* timer, Severity severity = Severity::DEBUG) {
  class Impl final : public TraceSinkInterface {
   public:
    Impl(Timer* timer) : timer_(timer) {}

    void Trace(TraceOutput output) {
      char sev = '?';
      switch (output.severity) {
        case Severity::DEBUG:
          sev = 'D';
        case Severity::INFO:
          sev = 'I';
        case Severity::WARNING:
          sev = 'W';
        case Severity::ERROR:
          sev = 'E';
      }
      std::cout << sev << timer_->Now() << " " << output.file << ":"
                << output.line << ": " << output.message << std::endl;
    }
    void Done() { delete this; }

   private:
    Timer* const timer_;
  };
  return TraceSink(severity, new Impl(timer));
}

}  // namespace overnet

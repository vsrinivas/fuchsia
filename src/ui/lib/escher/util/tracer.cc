// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/lib/escher/util/tracer.h"

#include <iostream>
#include <sstream>

#include "src/lib/files/file.h"

namespace escher {

namespace {
Tracer* g_tracer = nullptr;
}  // anonymous namespace

Tracer::Tracer() {
  FXL_DCHECK(!g_tracer);
  g_tracer = this;
  events_.reserve(10000000);
}

inline void WriteEvent(std::ostream& str, const Tracer::Event& event) {
  str << "\t\t{ \"name\": \"" << event.name << "\", \"cat\": \""
      << event.category << "\", \"ph\": \"" << event.phase << "\", \"pid\": \""
      << 1 << "\", \"tid\": \"" << 1 << "\", \"ts\": \"" << event.microseconds
      << "\" }";
}

Tracer::~Tracer() {
  FXL_DCHECK(g_tracer == this);
  g_tracer = nullptr;

  std::ostringstream str;

  str << "{\n\t\"traceEvents\": [\n";

  const size_t count = events_.size();
  for (size_t i = 0; i < count - 1; ++i) {
    WriteEvent(str, events_[i]);
    str << ",\n";
  }
  WriteEvent(str, events_[count - 1]);
  str << "\n\t],"
      << "\n\t\"displayTimeUnit\": \"ms\""
      << "\n}\n";

  files::WriteFile("escher.trace", str.str().data(), str.str().length());
  FXL_LOG(INFO) << "Wrote trace file: escher.trace";
}

void Tracer::AddTraceEvent(char phase, const char* category, const char* name) {
  events_.push_back(
      {phase, category, name, stopwatch_.GetElapsedMicroseconds()});
}

Tracer* GetTracer() { return g_tracer; }

}  // namespace escher

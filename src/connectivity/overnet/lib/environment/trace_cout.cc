// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/overnet/lib/environment/trace_cout.h"

#include <iostream>

namespace overnet {

std::mutex TraceCout::mu_;

void TraceCout::Render(TraceOutput output) {
  std::lock_guard<std::mutex> lock(mu_);

  char sev = '?';
  switch (output.severity) {
    case Severity::DEBUG:
      sev = 'D';
      break;
    case Severity::TRACE:
      sev = 'T';
      break;
    case Severity::INFO:
      sev = 'I';
      break;
    case Severity::WARNING:
      sev = 'W';
      break;
    case Severity::ERROR:
      sev = 'E';
      break;
  }

  auto padded = [](ssize_t len, auto fn) {
    std::ostringstream out;
    fn(out);
    auto s = out.str();
    if (len >= 0) {
      while (s.length() < size_t(len)) {
        s += ' ';
      }
    } else if (s.length() < size_t(-len)) {
      s = std::string(-len - s.length(), ' ') + s;
    }
    return s;
  };

  const char* file = strrchr(output.file, '/');
  if (file == nullptr) {
    file = output.file;
  } else {
    file++;
  }

  auto pfx = padded(12, [=](auto& out) {
    out << sev;
    if (timer_) {
      out << timer_->Now();
    }
  });

  std::cout << pfx << padded(-40, [=](auto& out) { out << file << ":"; })
            << padded(4, [=](auto& out) { out << output.line; }) << ": "
            << output.message;
  bool opened_tags = false;
  auto maybe_open_tags = [&] {
    if (!opened_tags) {
      static const size_t kMessagePad = 120;
      if (strlen(output.message) < kMessagePad) {
        std::cout << std::string(kMessagePad - strlen(output.message), ' ');
      }
      std::cout << " // ";
      opened_tags = true;
    }
  };
  if (output.op.type() != OpType::INVALID) {
    maybe_open_tags();
    std::cout << " OP:" << output.op;
  }
  output.scopes.Visit([&](Module module, void* p) {
    maybe_open_tags();
    std::cout << ' ' << module << ":" << p;
  });
  std::cout << std::endl;
}

}  // namespace overnet

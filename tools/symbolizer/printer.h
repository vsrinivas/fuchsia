// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_SYMBOLIZER_PRINTER_H_
#define TOOLS_SYMBOLIZER_PRINTER_H_

#include <ostream>
#include <string_view>
#include <utility>

namespace symbolizer {

// Wrapper for the output stream. It keeps a context string set by the LogParser which could contain
// information such as timestamp, process id, thread id, etc, so that each line of the output from
// the Symbolizer will be prefixed with the context automatically.
class Printer {
 public:
  explicit Printer(std::ostream& output) : output_(output) {}

  void SetContext(std::string_view context) { context_ = context; }

  template <typename T>
  void OutputWithContext(T&& string) {
    output_ << context_ << std::forward<T>(string) << std::endl;
  }

  template <typename T>
  void OutputRaw(T&& string) {
    output_ << std::forward<T>(string) << std::endl;
  }

 private:
  std::ostream& output_;
  std::string context_;
};

}  // namespace symbolizer

#endif  // TOOLS_SYMBOLIZER_PRINTER_H_

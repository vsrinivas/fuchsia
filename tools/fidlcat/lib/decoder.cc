// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/syslog/cpp/macros.h>
#include <zircon/types.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <ostream>
#include <vector>

#include "src/developer/debug/zxdb/client/breakpoint.h"
#include "src/developer/debug/zxdb/client/frame.h"
#include "src/developer/debug/zxdb/client/memory_dump.h"
#include "src/developer/debug/zxdb/client/process.h"
#include "src/developer/debug/zxdb/client/step_thread_controller.h"
#include "src/developer/debug/zxdb/client/thread.h"
#include "src/developer/debug/zxdb/symbols/symbol.h"
#include "tools/fidlcat/lib/event.h"
#include "tools/fidlcat/lib/syscall_decoder.h"

namespace fidlcat {

// TODO(b 42261): This wouldn't be necessary if zxdb would clean the paths.
std::vector<std::string_view> CleanPath(const zxdb::FileLine& file_line) {
  std::vector<std::string_view> path_elements;
  int ignore_leading_dot_dot = 2;
  size_t pos = 0;
  for (;;) {
    size_t start = pos;
    pos = file_line.file().find('/', pos);
    if (pos == std::string::npos) {
      path_elements.emplace_back(
          std::string_view(file_line.file().c_str() + start, file_line.file().size() - start));
      break;
    }
    std::string_view element(file_line.file().c_str() + start, pos - start);
    if ((ignore_leading_dot_dot > 0) && (element.compare("..") == 0)) {
      // The paths returned by zxdb are not relative to the Fuchsia root directory.
      // We must ignore the first two ".." to be relative to the root directory.
      --ignore_leading_dot_dot;
    } else {
      path_elements.push_back(element);
      // Just in case the path didn't start with two "..".
      ignore_leading_dot_dot = 0;
    }
    ++pos;
  }
  // Remove the ".." we can.
  int destination = 0;
  for (const auto& element : path_elements) {
    if (element.compare(".") != 0) {
      if ((element.compare("..") == 0) && (destination > 0) &&
          (path_elements[destination - 1].compare("..") != 0)) {
        --destination;
      } else {
        path_elements[destination++] = element;
      }
    }
  }
  path_elements.resize(destination);
  return path_elements;
}

void DisplayStackFrame(const std::vector<zxdb::Location>& caller_locations,
                       fidl_codec::PrettyPrinter& printer) {
  bool header_on_every_line = printer.header_on_every_line();
  // We want a header on every stack frame line.
  printer.set_header_on_every_line(true);
  for (const auto& location : caller_locations) {
    if (location.is_valid()) {
      const zxdb::LazySymbol& symbol = location.symbol();
      printer << fidl_codec::YellowBackground << "at " << fidl_codec::Red;
      if (location.is_symbolized()) {
        std::vector<std::string_view> file = CleanPath(location.file_line());
        // Display the optimized path.
        const char* separator = "";
        for (const auto& item : file) {
          printer << separator << item;
          separator = "/";
        }
        printer << fidl_codec::ResetColor << fidl_codec::YellowBackground << ':' << fidl_codec::Blue
                << location.file_line().line() << ':' << location.column()
                << fidl_codec::ResetColor;
      } else {
        printer << std::hex << location.address() << fidl_codec::ResetColor << std::dec;
      }
      if (symbol.is_valid()) {
        printer << ' ' << symbol.Get()->GetFullName();
      }
      printer << '\n';
    }
  }
  printer.set_header_on_every_line(header_on_every_line);
}

void CopyStackFrame(const std::vector<zxdb::Location>& caller_locations,
                    std::vector<Location>* locations) {
  for (const auto& location : caller_locations) {
    if (location.is_valid()) {
      std::string path;
      uint32_t line = 0;
      uint32_t column = 0;
      if (location.is_symbolized()) {
        std::vector<std::string_view> file = CleanPath(location.file_line());
        // Copies the optimized path.
        const char* separator = "";
        for (const auto& item : file) {
          path += separator;
          path += item;
          separator = "/";
        }
        line = location.file_line().line();
        column = location.column();
      }
      std::string empty_symbol;
      const zxdb::LazySymbol& symbol = location.symbol();
      locations->emplace_back(path, line, column, location.address(),
                              symbol.is_valid() ? symbol.Get()->GetFullName() : empty_symbol);
    }
  }
}

}  // namespace fidlcat

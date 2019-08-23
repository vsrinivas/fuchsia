// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/system/public/zircon/types.h>

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
#include "src/developer/debug/zxdb/client/register.h"
#include "src/developer/debug/zxdb/client/step_thread_controller.h"
#include "src/developer/debug/zxdb/client/thread.h"
#include "src/developer/debug/zxdb/symbols/symbol.h"
#include "src/lib/fxl/logging.h"
#include "tools/fidlcat/lib/syscall_decoder.h"

namespace fidlcat {

void DisplayStackFrame(const Colors& colors, std::string_view line_header,
                       const std::vector<zxdb::Location>& caller_locations, std::ostream& os) {
  for (const auto& location : caller_locations) {
    if (location.is_valid()) {
      const zxdb::LazySymbol& symbol = location.symbol();
      os << line_header << colors.yellow_background << "at " << colors.red;
      if (location.is_symbolized()) {
        const zxdb::FileLine& file_line = location.file_line();
        // Split the file path.
        std::vector<std::string_view> file;
        int ignore_leading_dot_dot = 2;
        size_t pos = 0;
        for (;;) {
          size_t start = pos;
          pos = file_line.file().find('/', pos);
          if (pos == std::string::npos) {
            file.emplace_back(std::string_view(file_line.file().c_str() + start,
                                               file_line.file().size() - start));
            break;
          }
          std::string_view item(file_line.file().c_str() + start, pos - start);
          if ((ignore_leading_dot_dot > 0) && (item.compare("..") == 0)) {
            // The path returned by zxdb are not relative to the Fuchsia root directory.
            // We must ignore the first two ".." to be relative to the root directory.
            --ignore_leading_dot_dot;
          } else {
            file.push_back(item);
            // Just in case the path didn't start with two "..".
            ignore_leading_dot_dot = 0;
          }
          ++pos;
        }
        // Remove the ".." we can.
        int destination = 0;
        for (const auto& item : file) {
          if (item.compare(".") != 0) {
            if ((item.compare("..") == 0) && (destination > 0) &&
                (file[destination - 1].compare("..") != 0)) {
              --destination;
            } else {
              file[destination++] = item;
            }
          }
        }
        file.resize(destination);
        // Display the optimized path.
        const char* separator = "";
        for (const auto& item : file) {
          os << separator << item;
          separator = "/";
        }
        os << colors.reset << colors.yellow_background << ':' << colors.blue << file_line.line()
           << colors.reset;
      } else {
        os << std::hex << location.address() << colors.reset << std::dec;
      }
      if (symbol.is_valid()) {
        os << ' ' << symbol.Get()->GetFullName();
      }
      os << '\n';
    }
  }
}

}  // namespace fidlcat

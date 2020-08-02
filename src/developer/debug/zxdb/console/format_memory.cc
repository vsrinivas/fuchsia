// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/console/format_memory.h"

#include <inttypes.h>

#include <limits>

#include "src/developer/debug/zxdb/client/memory_dump.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace zxdb {

namespace {

const char kUnknownByte[] = "??";
const char kNonAscii = ' ';  // When printing ASCII and the character is not in range.

bool IsPrintableAscii(uint8_t c) { return c >= ' ' && c < 0x7f; }

// Formats the given |address|. The |begin_address| is given so we can compute the offset.
std::string GetAddressString(MemoryFormatOptions::AddressMode mode, int addr_width,
                             uint64_t begin_address, uint64_t address) {
  switch (mode) {
    case MemoryFormatOptions::kNoAddresses:
      return std::string();
    case MemoryFormatOptions::kAddresses:
      return fxl::StringPrintf("0x%0*" PRIx64 ":  ", addr_width, address);
    case MemoryFormatOptions::kOffsets:
      return fxl::StringPrintf("+0x%0*" PRIx64 ":  ", addr_width, address - begin_address);
  }
  return std::string();
}

}  // namespace

// Optimized for simplicity over speed. But this does not use the table output to avoid having giant
// table computations for large memory dumps.
OutputBuffer FormatMemory(const MemoryDump& dump, uint64_t begin, uint32_t size,
                          const MemoryFormatOptions& opts) {
  OutputBuffer out;

  // Special-case 0 size because the "max_addr" computation below doesn't make any sense in that
  // context.
  if (size == 0) {
    out.Append(Syntax::kComment, GetAddressString(opts.address_mode, 0, begin, begin));
    out.Append("\n");
    return out;
  }

  // Compute the last address we'll print. Watch for overflow.
  uint64_t max_addr = std::numeric_limits<uint64_t>::max();
  if (max_addr - size > begin)
    max_addr = begin + size - 1;

  // Max address number character width for the digits to be padded out to (not including "0x"
  // prefix).
  int addr_width;
  switch (opts.address_mode) {
    case MemoryFormatOptions::kNoAddresses:
      addr_width = 0;
      break;
    case MemoryFormatOptions::kAddresses:
      addr_width = static_cast<int>(fxl::StringPrintf("%" PRIx64, max_addr).size());
      break;
    case MemoryFormatOptions::kOffsets:
      addr_width = static_cast<int>(fxl::StringPrintf("%" PRIx32, size).size());
      break;
  }

  uint64_t cur = begin;  // Current address being printed.
  std::string line;      // These string buffers are outside the loop to prevent reallocation.
  OutputBuffer values;
  std::string ascii;
  bool done = false;
  while (!done) {  // Line loop
    // Compute address at beginning of line.
    out.Append(Syntax::kComment,
               GetAddressString(opts.address_mode, addr_width, dump.address(), cur));

    values.Clear();
    ascii = "  |";
    for (int i = 0; i < opts.values_per_line; i++) {  // Value loop.
      // Separator between values.
      if (i > 0) {
        if (!done && opts.separator_every > 0 && (i % opts.separator_every) == 0) {
          values.Append(Syntax::kComment, "-");
        } else {
          values.Append(" ");
        }
      }

      if (!done) {
        uint8_t byte;
        if (dump.GetByte(cur, &byte)) {
          values.Append(fxl::StringPrintf("%02x", static_cast<int>(byte)));
          if (IsPrintableAscii(byte)) {
            ascii.push_back(static_cast<char>(byte));
          } else {
            ascii.push_back(kNonAscii);
          }
        } else {
          values.Append(Syntax::kComment, kUnknownByte);
          ascii.push_back(kNonAscii);
        }

        // Carefully only increment address if it won't overflow.
        if (cur == max_addr) {
          done = true;
        } else {
          cur++;
        }
      } else {
        // Line is done, but we still want padding for values.
        values.Append("  ");
        ascii.push_back(' ');
      }
    }

    // Append the constructed elements.
    out.Append(std::move(values));
    if (opts.show_ascii)
      out.Append(Syntax::kComment, std::move(ascii));
    out.Append("\n");
  }

  return out;
}

}  // namespace zxdb

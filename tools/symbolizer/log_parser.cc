// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/symbolizer/log_parser.h"

#include <charconv>
#include <string_view>

#include "src/lib/fxl/strings/split_string.h"

namespace symbolizer {

namespace {

// Converts the string in dec or hex into an integer. Returns whether the conversion is complete.
template <typename int_t>
bool ParseInt(std::string_view string, int_t &i) {
  if (string.empty())
    return false;

  const char *begin = string.begin();
  const char *end = begin + string.size();
  int base = 10;
  if (string.size() > 2 && string[0] == '0' && string[1] == 'x') {
    base = 16;
    begin += 2;
  }
  return std::from_chars(begin, end, i, base).ptr == end;
}

}  // namespace

bool LogParser::ProcessOneLine() {
  std::string line;

  std::getline(input_, line);
  if (input_.eof() && line.empty()) {
    return false;
  }

  auto start = line.find("{{{");
  auto end = std::string::npos;

  if (start != std::string::npos) {
    end = line.find("}}}", start);
  }
  if (end != std::string::npos) {
    std::string_view line_view(line);
    printer_->SetContext(line_view.substr(0, start));
    if (ProcessMarkup(line_view.substr(start + 3, end - start - 3))) {
      // Skip outputting only if we have the starting and the ending braces and the markup is valid.
      return true;
    }
  }

  printer_->OutputRaw(line);
  return true;
}

bool LogParser::ProcessMarkup(std::string_view markup) {
  auto splitted = fxl::SplitString(markup, ":", fxl::kKeepWhitespace, fxl::kSplitWantAll);
  if (splitted.empty()) {
    return false;
  }

  auto tag = splitted[0];

  if (tag == "reset") {
    symbolizer_->Reset();
    return true;
  }

  if (tag == "module") {
    // module:0x{id}:{name}:elf:{build_id}
    if (splitted.size() < 5)
      return false;

    uint64_t id;
    if (!ParseInt(splitted[1], id) || splitted[3] != "elf")
      return false;

    symbolizer_->Module(id, splitted[2], splitted[4]);
    return true;
  }

  if (tag == "mmap") {
    // mmap:0x{address}:0x{size}:load:0x{module_id}:r?w?x?:0x{module_offset}
    if (splitted.size() < 7)
      return false;

    uint64_t address;
    uint64_t size;
    uint64_t module_id;
    uint64_t module_offset;

    if (!ParseInt(splitted[1], address) || !ParseInt(splitted[2], size) ||
        !ParseInt(splitted[4], module_id) || !ParseInt(splitted[6], module_offset) ||
        splitted[3] != "load")
      return false;

    symbolizer_->MMap(address, size, module_id, module_offset);
    return true;
  }

  if (tag == "bt") {
    // bt:{frame_id}:{address}(:ra|:pc)?(:msg)?
    if (splitted.size() < 3)
      return false;

    int frame_id;
    uint64_t address;
    Symbolizer::AddressType type = Symbolizer::AddressType::kUnknown;
    std::string_view message;

    if (!ParseInt(splitted[1], frame_id) || !ParseInt(splitted[2], address))
      return false;

    // Optional suffix(es).
    if (splitted.size() >= 4) {
      if (splitted[3] == "ra") {
        type = Symbolizer::AddressType::kReturnAddress;
      } else if (splitted[3] == "pc") {
        type = Symbolizer::AddressType::kProgramCounter;
      } else {
        message = splitted[3];
      }
      if (splitted.size() >= 5) {
        message = splitted[4];
      }
    }
    symbolizer_->Backtrace(frame_id, address, type, message);
    return true;
  }

  return false;
}

}  // namespace symbolizer

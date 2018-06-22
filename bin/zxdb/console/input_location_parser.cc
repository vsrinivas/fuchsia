// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/console/input_location_parser.h"

#include "garnet/bin/zxdb/client/frame.h"
#include "garnet/bin/zxdb/client/symbols/location.h"
#include "garnet/bin/zxdb/console/command_utils.h"

namespace zxdb {

Err ParseInputLocation(const Frame* frame, const std::string& input,
                       InputLocation* location) {
  if (input.empty())
    return Err("Passed empty location.");

  // Check for one colon. Two colons is a C++ member function.
  size_t colon = input.find(':');
  if (colon != std::string::npos && colon < input.size() - 1 &&
      input[colon + 1] != ':') {
    // <file>:<line> format.
    std::string file = input.substr(0, colon);

    uint64_t line = 0;
    Err err = StringToUint64(input.substr(colon + 1), &line);
    if (err.has_error())
      return err;

    location->type = InputLocation::Type::kLine;
    location->line = FileLine(std::move(file), static_cast<int>(line));
    return Err();
  }

  if (input[0] == '*') {
    // *<address> format
    std::string addr_str = input.substr(1);
    Err err = StringToUint64(addr_str, &location->address);
    if (err.has_error())
      return err;

    location->type = InputLocation::Type::kAddress;
    return Err();
  }

  uint64_t line = 0;
  Err err = StringToUint64(input, &line);
  if (err.has_error()) {
    // Not a number, assume symbol.
    location->type = InputLocation::Type::kSymbol;
    location->symbol = input;
    return Err();
  }

  // Just a number, use the file name from the specified frame.
  if (!frame) {
    return Err(
        "There is no current frame to get a file name, you'll have to "
        "specify one.");
  }
  const Location& frame_location = frame->GetLocation();
  if (frame_location.file_line().file().empty()) {
    return Err(
        "The current frame doesn't have a file name to use, you'll "
        "have to specify one.");
  }
  location->type = InputLocation::Type::kLine;
  location->line =
      FileLine(frame_location.file_line().file(), static_cast<int>(line));
  return Err();
}

}  // namespace zxdb

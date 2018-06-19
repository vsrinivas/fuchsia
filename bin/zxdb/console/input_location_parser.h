// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "garnet/bin/zxdb/client/err.h"
#include "garnet/bin/zxdb/client/input_location.h"

namespace zxdb {

class Command;
class Frame;

// Parses a given input from the user as a location. The frame is used for
// context if the user specifies a line number with no file name. This does
// not handle the case where no location is specified (some commands, like
// "break", might use this to indicate the current location, but many other
// commands don't support this format).
Err ParseInputLocation(const Frame* frame, const std::string& input,
                       InputLocation* location);

// Generates help for a command describing the parsing of locations. The
// parameter is a string containing the name of the command.
#define LOCATION_ARG_HELP(cmd) \
    "  <symbol>\n" \
    "    " cmd " main\n" \
    "    " cmd " Foo::Bar\n" \
    "\n" \
    "  <file>:<line>\n" \
    "    " cmd " foo.cc:123\n" \
    "\n" \
    "    ▷ To disambiguate different files with the same name, include\n" \
    "      directory names preceeding the name (from the right).\n" \
    "\n" \
    "  <line number> (within the frame's file)\n" \
    "    " cmd " 123\n" \
    "\n" \
    "  *<address>\n" \
    "    " cmd " *0x7d12362f0\n"

}  // namespace zxdb

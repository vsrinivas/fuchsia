// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <vector>

#include "garnet/bin/zxdb/symbols/input_location.h"
#include "garnet/bin/zxdb/symbols/location.h"
#include "src/developer/debug/zxdb/common/err.h"

namespace zxdb {

class Command;
class Frame;
class ProcessSymbols;

// Parses a given input from the user to an InputLocation.
//
// The optional frame is used for context if the user specifies a line number
// with no file name. If the frame is null, the line-number-only format will
// generate an error.
//
// This does not handle the case where no location is specified (some commands,
// like "break", might use this to indicate the current location, but many
// other commands don't support this format).
Err ParseInputLocation(const Frame* optional_frame, const std::string& input,
                       InputLocation* location);

// Parses the input and generates a list of matches. No matches will generate
// an error. This can take either a pre-parsed InputLocation, or can parse
// the input itself.
//
// Set |symbolize| to make the output locations symbolized. This will be
// slightly slower. If you just need the addresses, pass false.
Err ResolveInputLocations(const ProcessSymbols* process_symbols,
                          const InputLocation& input_location, bool symbolize,
                          std::vector<Location>* locations);
Err ResolveInputLocations(const ProcessSymbols* process_symbols,
                          const Frame* optional_frame, const std::string& input,
                          bool symbolize, std::vector<Location>* locations);

// Resolves the given input string to a Location object. Returns an error
// parsing or if the location can not be resolved or resolves to more than one
// address.
//
// Set |symbolize| to make the output |*location| symbolized. This will be
// slightly slower. If you just need the address, pass false.
Err ResolveUniqueInputLocation(const ProcessSymbols* process_symbols,
                               const InputLocation& input_location,
                               bool symbolize, Location* location);
Err ResolveUniqueInputLocation(const ProcessSymbols* process_symbols,
                               const Frame* optional_frame,
                               const std::string& input, bool symbolize,
                               Location* location);

// Generates help for a command describing the parsing of locations. The
// parameter is a string containing the name of the command.
#define LOCATION_ARG_HELP(cmd)                                            \
  "  <symbol>\n"                                                          \
  "    " cmd                                                              \
  " main\n"                                                               \
  "    " cmd                                                              \
  " Foo::Bar\n"                                                           \
  "\n"                                                                    \
  "  <file>:<line>\n"                                                     \
  "    " cmd                                                              \
  " foo.cc:123\n"                                                         \
  "\n"                                                                    \
  "    ▷ To disambiguate different files with the same name, include\n" \
  "      directory names preceding the name (from the right).\n"          \
  "\n"                                                                    \
  "  <line number> (within the frame's file)\n"                           \
  "    " cmd                                                              \
  " 123\n"                                                                \
  "\n"                                                                    \
  "    ▷ All decimal integers are considered line numbers.\n"           \
  "\n"                                                                    \
  "  0x<address>\n"                                                       \
  "  *<address>\n"                                                        \
  "    " cmd                                                              \
  " 0x7d12362f0\n"                                                        \
  "\n"                                                                    \
  "    ▷ All hexadecimal numbers are considered addresses. Precede\n"   \
  "      decimal numbers with * to force interpretation as an address.\n"

}  // namespace zxdb

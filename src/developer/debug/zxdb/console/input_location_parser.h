// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_CONSOLE_INPUT_LOCATION_PARSER_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_CONSOLE_INPUT_LOCATION_PARSER_H_

#include <vector>

#include "src/developer/debug/zxdb/common/err.h"
#include "src/developer/debug/zxdb/symbols/input_location.h"
#include "src/developer/debug/zxdb/symbols/location.h"

namespace zxdb {

class Command;
class Frame;
class ProcessSymbols;

// Parses a given input from the user to an InputLocation. Symbolic names ("MyFunction") are treated
// as global names and there is no processing done on these. For context-aware handling of function
// names, see ParseLocalInputLocation.
//
// The optional frame is used for context if the user specifies a line number with no file name. If
// the frame is null, the line-number-only format will generate an error.
//
// This does not handle the case where no location is specified (some commands, like "break", might
// use this to indicate the current location, but many other commands don't support this format).
Err ParseGlobalInputLocation(const Frame* optional_frame, const std::string& input,
                             InputLocation* location);

// The same as ParseGlobalInputLocation but this will additionally find unqualified functions on the
// current class if there is one. For file/line and address based inputs, this will be identical to
// ParseGlobalInputLocation().
//
// In the symbolic name case, it can produce more than one location by qualifying the matched class
// member function name so it can be resolved in a global context. It will ALSO return an
// InputLocation that corresponds to the global symbol name.
//
// For example, if the input is "Func" this might return two things:
//   - "::some_namespace::MyClass::Func" (match from current frame).
//   - "Func" (so all global functions will still be matched when the symbols are queried).
// If there's no "local" match or the optional_frame is null, this will return only the second
// unmodified "Func" version.
//
// This is designed to return the maximal number of matches given the current context that can
// be resolved later without ambiguity (breakpoints will need to match shared libraries loaded
// later when the current context won't be known).
Err ParseLocalInputLocation(const Frame* optional_frame, const std::string& input,
                            std::vector<InputLocation>* locations);

// Parses the input and generates a list of matches. No matches will generate an error. This can
// take either a pre-parsed InputLocation, or can parse the input itself.
//
// Set |symbolize| to make the output locations symbolized. This will be slightly slower. If you
// just need the addresses, pass false.
//
// The |optional_frame| will be passed for context to ParseLocalInputLocation() above to handle
// finding names based on the current scope. See that for the behavior of local functions
// definitions.
//
// Underneath final resolution is done by ResolvePermissiveInputLocations() so will match functions
// in any namespace unless globally qualified.
Err ResolveInputLocations(const ProcessSymbols* process_symbols, const Frame* optional_frame,
                          const std::string& input, bool symbolize,
                          std::vector<Location>* locations);

// Variant of the above that takes a pre-parsed list of input locations.
Err ResolveInputLocations(const ProcessSymbols* process_symbols,
                          const std::vector<InputLocation>& input_locations, bool symbolize,
                          std::vector<Location>* locations);

// Resolves the given input string to a Location object. Returns an error parsing or if the location
// can not be resolved or resolves to more than one address.
//
// Set |symbolize| to make the output |*location| symbolized. This will be slightly slower. If you
// just need the address, pass false.
//
// The variant with optional_frame will use ParseLocalInputLocation() above. See that for the
// behavior of local functions definitions.
Err ResolveUniqueInputLocation(const ProcessSymbols* process_symbols,
                               const InputLocation& input_location, bool symbolize,
                               Location* location);
Err ResolveUniqueInputLocation(const ProcessSymbols* process_symbols,
                               const std::vector<InputLocation>& input_location, bool symbolize,
                               Location* location);
Err ResolveUniqueInputLocation(const ProcessSymbols* process_symbols, const Frame* optional_frame,
                               const std::string& input, bool symbolize, Location* location);

// Autocomplete for input locations.
void CompleteInputLocation(const Command& command, const std::string& prefix,
                           std::vector<std::string>* completions);

// Generates help for a command describing the parsing of locations. The parameter is a string
// containing the name of the command.
// clang-format off
#define LOCATION_ARG_HELP(cmd)                                               \
  "  <symbol>\n"                                                             \
  "    " cmd " main\n"                                                       \
  "    " cmd " Foo::Bar\n"                                                   \
  "\n"                                                                       \
  "    ▷ This will match functions in the current class if there is one.\n"  \
  "      To override, prefix with \"::\" as in \"" cmd " ::Foo::Bar\".\n"    \
  "\n"                                                                       \
  "  <file>:<line>\n"                                                        \
  "    " cmd " foo.cc:123\n"                                                 \
  "\n"                                                                       \
  "    ▷ To disambiguate different files with the same name, include\n"      \
  "      directory names preceding the name (from the right).\n"             \
  "\n"                                                                       \
  "  <line number> (within the frame's file)\n"                              \
  "    " cmd " 123\n"                                                        \
  "\n"                                                                       \
  "    ▷ All decimal integers are considered line numbers.\n"                \
  "\n"                                                                       \
  "  0x<address>\n"                                                          \
  "  *<address>\n"                                                           \
  "    " cmd " 0x7d12362f0\n"                                                \
  "\n"                                                                       \
  "    ▷ All hexadecimal numbers are considered addresses. Precede\n"        \
  "      decimal numbers with * to force interpretation as an address.\n"
// clang-format on

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_CONSOLE_INPUT_LOCATION_PARSER_H_

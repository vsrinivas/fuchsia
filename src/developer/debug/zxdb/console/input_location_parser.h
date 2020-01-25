// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_CONSOLE_INPUT_LOCATION_PARSER_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_CONSOLE_INPUT_LOCATION_PARSER_H_

#include <vector>

#include "lib/fit/function.h"
#include "src/developer/debug/zxdb/common/err.h"
#include "src/developer/debug/zxdb/common/err_or.h"
#include "src/developer/debug/zxdb/symbols/input_location.h"
#include "src/developer/debug/zxdb/symbols/location.h"
#include "src/lib/fxl/memory/ref_counted.h"

namespace zxdb {

class Command;
class EvalContext;
class Frame;
class ProcessSymbols;

// Parses a given input from the user to an InputLocation. Symbolic names ("MyFunction") are treated
// as global names and there is no processing done on these. For context-aware handling of function
// names, see ParseLocalInputLocation.
//
// The Location is used to resolve line numbers in the current file. The location need not have a
// current file (it can be an empty location or an unsymbolized one) in which case numeric input
// will fail.
//
// This does not handle the case where no location is specified (some commands, like "break", might
// use this to indicate the current location, but many other commands don't support this format).
//
// This does not handle expressions, see EvalGlobalInputLocation() below.
Err ParseGlobalInputLocation(const Location& location, const std::string& input,
                             InputLocation* output);

// Like ParseGlobalInputLocation() but also accepts expressions preceeded with a "*". Because
// evaluating expressions may be asynchronous, this function is too.
//
// If the input location is an expression and the thing it points to has an intrinsic size, that
// size will be passed as the second parameter of the callback.
//
// The callback follows "expression" rules in that it will be evaluted from within the stack of this
// function if the result is synchronously available.
void EvalGlobalInputLocation(
    const fxl::RefPtr<EvalContext> eval_context, const Location& location, const std::string& input,
    fit::callback<void(ErrOr<InputLocation>, std::optional<uint32_t> size)> cb);

// The same as ParseGlobalInputLocation() but this will additionally find unqualified functions on
// the current class if there is one. For file/line and address based inputs, this will be identical
// to ParseGlobalInputLocation().
//
// The Frame* variant will extract the process symbols and current location from the frame if
// possible.
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
//
// The ProcessSymbols pointer can be null in which case this will have the same behavior as
// ParseGlobalInputLocation() (this simplifies some callers which need to handle both cases).
Err ParseLocalInputLocation(const ProcessSymbols* optional_process_symbols,
                            const Location& location, const std::string& input,
                            std::vector<InputLocation>* output);
Err ParseLocalInputLocation(const Frame* optional_frame, const std::string& input,
                            std::vector<InputLocation>* output);

// Like ParseLocalInputLocation() but also accepts expressions preceeded with a "*". Because
// evaluating expressions may be asynchronous, this function is too.
//
// If the input location is an expression and the thing it points to has an intrinsic size, that
// size will be passed as the second parameter of the callback.
//
// The callback follows "expression" rules in that it will be evaluted from within the stack of this
// function if the result is synchronously available.
void EvalLocalInputLocation(
    const fxl::RefPtr<EvalContext>& eval_context, const Location& location,
    const std::string& input,
    fit::callback<void(ErrOr<std::vector<InputLocation>>, std::optional<uint32_t> size)> cb);

// Parses the input and generates a list of matches. No matches will generate an error. This can
// take either a pre-parsed InputLocation, or can parse the input itself.
//
// The Frame* variant will extract the process symbols and current location from the frame if
// possible.
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
Err ResolveInputLocations(const ProcessSymbols* process_symbols, const Location& location,
                          const std::string& input, bool symbolize, std::vector<Location>* output);
Err ResolveInputLocations(const Frame* optional_frame, const std::string& input, bool symbolize,
                          std::vector<Location>* output);

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
// These first two variants take pre-parsed InputLocation(s):
Err ResolveUniqueInputLocation(const ProcessSymbols* process_symbols,
                               const InputLocation& input_location, bool symbolize,
                               Location* output);
Err ResolveUniqueInputLocation(const ProcessSymbols* process_symbols,
                               const std::vector<InputLocation>& input_location, bool symbolize,
                               Location* output);
// These variants parse the input. The Frame* variant will extract the process symbols and current
// location from the frame if possible.
Err ResolveUniqueInputLocation(const ProcessSymbols* process_symbols, const Location& location,
                               const std::string& input, bool symbolize, Location* output);
Err ResolveUniqueInputLocation(const Frame* optional_frame, const std::string& input,
                               bool symbolize, Location* output);

// Autocomplete for input locations.
void CompleteInputLocation(const Command& command, const std::string& prefix,
                           std::vector<std::string>* completions);

// Generates help for a command describing the parsing of locations. The parameter is a string
// containing the name of the command.
// clang-format off
#define LOCATION_ARG_HELP(cmd)                                                 \
  "  <symbol>\n"                                                               \
  "      " cmd " main\n"                                                       \
  "      " cmd " Foo::Bar\n"                                                   \
  "\n"                                                                         \
  "      ▷ This will match functions in the current class if there is one.\n"  \
  "        To override, prefix with \"::\" as in \"" cmd " ::Foo::Bar\".\n"    \
  "\n"                                                                         \
  "  <file>:<line>\n"                                                          \
  "      " cmd " foo.cc:123\n"                                                 \
  "\n"                                                                         \
  "      ▷ To disambiguate different files with the same name, include\n"      \
  "        directory names preceding the name (from the right).\n"             \
  "\n"                                                                         \
  "  <line number> (within the frame's file)\n"                                \
  "      " cmd " 123\n"                                                        \
  "\n"                                                                         \
  "      ▷ All decimal integers are considered line numbers.\n"                \
  "\n"                                                                         \
  "  0x<address>\n"                                                            \
  "      " cmd " 0x7d12362f0\n"                                                \
  "\n"                                                                         \
  "      ▷ All hexadecimal numbers are considered addresses. Precede\n"        \
  "        decimal numbers with * to force interpretation as an address.\n"    \
  "\n"

// Append this to LOCATION_ARG_HELP(cmd) if the command supports expressions via
// Eval*InputLocation().
#define LOCATION_EXPRESSION_HELP(cmd)                                              \
  "  \"*<expression>\"\n"                                                          \
  "      " cmd " *0x7d12362f0\n"                                                   \
  "      " cmd " *&my_thing\n"                                                     \
  "      " cmd " \"*my_array[0]->some_pointer\"\n"                                 \
  "\n"                                                                             \
  "      ▷ An arbitrary expression can be evaluated and the result will be\n"      \
  "        interpreted as an address. If the result is a pointer, that\n"          \
  "        pointer's value will be used. If the result is an integer or a\n"       \
  "        reference to an integer, the referenced integer will be interpreted\n"  \
  "        as a memory address.\n"                                                 \
  "\n"                                                                             \
  "        Anything with a space requires quotes.\n"                               \
  "\n"
// clang-format on

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_CONSOLE_INPUT_LOCATION_PARSER_H_

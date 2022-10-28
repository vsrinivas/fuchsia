// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/console/input_location_parser.h"

#include <inttypes.h>

#include <algorithm>
#include <limits>

#include "src/developer/debug/zxdb/client/frame.h"
#include "src/developer/debug/zxdb/client/process.h"
#include "src/developer/debug/zxdb/client/target.h"
#include "src/developer/debug/zxdb/client/thread.h"
#include "src/developer/debug/zxdb/console/command.h"
#include "src/developer/debug/zxdb/console/command_utils.h"
#include "src/developer/debug/zxdb/console/format_location.h"
#include "src/developer/debug/zxdb/console/string_util.h"
#include "src/developer/debug/zxdb/expr/expr.h"
#include "src/developer/debug/zxdb/expr/expr_parser.h"
#include "src/developer/debug/zxdb/expr/find_name.h"
#include "src/developer/debug/zxdb/expr/found_name.h"
#include "src/developer/debug/zxdb/expr/permissive_input_location.h"
#include "src/developer/debug/zxdb/symbols/identifier.h"
#include "src/developer/debug/zxdb/symbols/index.h"
#include "src/developer/debug/zxdb/symbols/location.h"
#include "src/developer/debug/zxdb/symbols/module_symbols.h"
#include "src/developer/debug/zxdb/symbols/process_symbols.h"
#include "src/developer/debug/zxdb/symbols/target_symbols.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace zxdb {

namespace {

// Searches the current object ("this") in the frame for local matches of the given identifier.
// This will not return anything that exactly matches the input because it's assumed that value
// is always handled by the "global" case.
//
// For input locations it is not necessary to do a full lexical search beyond the local class
// because unqualified names will match any namespace in ResolveInputLocations(). That will catch
// all other instances of the symbol.
//
// If there is no current object or there are no matches, returns an empty vector. Otherwise returns
// tall matches with fully-qualified names.
std::vector<InputLocation> GetIdentifierMatchesOnThis(const ProcessSymbols* process_symbols,
                                                      const Location& loc,
                                                      const Identifier& input) {
  if (!loc.symbol())
    return {};
  const CodeBlock* code_block = loc.symbol().Get()->As<CodeBlock>();
  if (!code_block)
    return {};

  FindNameContext find_context(process_symbols, loc.symbol_context(), code_block);

  // Currently location matching matches only functions. We may need to broaden this in the
  // future as the needs of callers require.
  FindNameOptions find_options(FindNameOptions::kNoKinds);
  find_options.find_functions = true;
  find_options.max_results = std::numeric_limits<size_t>::max();  // Want everything

  std::vector<FoundName> found_local;
  FindMemberOnThis(find_context, find_options, ToParsedIdentifier(input), &found_local);

  std::vector<InputLocation> result;
  for (const auto& found : found_local) {
    Identifier found_ident = ToIdentifier(found.GetName());
    // The empty name check is paranoid in case the symbols are declaring weird things. Don't
    // duplicate the input which will be appended separately if needed.
    if (!found_ident.empty() && !found_ident.EqualsIgnoringQualification(input))
      result.emplace_back(found_ident);
  }
  return result;
}

}  // namespace

Err ParseGlobalInputLocation(const Location& location, const std::string& input,
                             InputLocation* output) {
  if (input.empty())
    return Err("Passed empty location.");

  // Check for one colon. Two colons is a C++ member function.
  size_t colon = input.find(':');
  const char kMissingFileError[] =
      "There is no current file name to use, you'll have to specify a file.";
  if (colon != std::string::npos && colon < input.size() - 1 && input[colon + 1] != ':') {
    // <file>:<line> format.
    std::string file = input.substr(0, colon);
    if (file.empty()) {
      // Empty file names take the current file name just like bare numbers.
      if (location.file_line().file().empty())
        return Err(kMissingFileError);
      file = location.file_line().file();
    }

    uint64_t line = 0;
    if (Err err = StringToUint64(input.substr(colon + 1), &line); err.has_error())
      return err;
    if (line == 0)
      return Err("Can't have a 0 line number.");

    *output = InputLocation(FileLine(std::move(file), static_cast<int>(line)));
    return Err();
  }

  // Hex numbers are addresses.
  if (CheckHexPrefix(input)) {
    uint64_t address = 0;
    if (Err err = StringToUint64(input, &address); err.has_error())
      return err;
    *output = InputLocation(address);
    return Err();
  }

  // Standalone non-hex numbers are line numbers, assume the current file name.
  uint64_t line = 0;
  Err err = StringToUint64(input, &line);
  if (!err.has_error()) {
    if (location.file_line().file().empty())
      return Err(kMissingFileError);

    *output = InputLocation(FileLine(location.file_line().file(), static_cast<int>(line)));
    return Err();
  }

  // Anything else, assume its an identifier.
  Identifier ident;
  if (err = ExprParser::ParseIdentifier(input, &ident); err.has_error())
    return err;

  *output = InputLocation(ident);
  return Err();
}

void EvalGlobalInputLocation(
    const fxl::RefPtr<EvalContext> eval_context, const Location& location, const std::string& input,
    fit::callback<void(ErrOr<InputLocation>, std::optional<uint32_t> size)> cb) {
  if (input.empty() || input[0] != '*') {
    // Not an expression, forward to the synchronous parser.
    InputLocation sync_result;
    if (Err err = ParseGlobalInputLocation(location, input, &sync_result); err.has_error())
      cb(err, std::nullopt);
    else
      cb(sync_result, std::nullopt);
    return;
  }

  // Everything following the * is the expression.
  std::string expr = input.substr(1);
  EvalExpression(
      expr, eval_context, true, [eval_context, cb = std::move(cb)](ErrOrValue result) mutable {
        if (result.has_error())
          return cb(RewriteCommandExpressionError(std::string(), result.err()), std::nullopt);

        uint64_t address = 0;
        std::optional<uint32_t> size;
        if (Err err = ValueToAddressAndSize(eval_context, result.value(), &address, &size);
            err.has_error()) {
          return cb(err, std::nullopt);
        }

        cb(InputLocation(address), size);
      });
}

Err ParseLocalInputLocation(const ProcessSymbols* optional_process_symbols,
                            const Location& location, const std::string& input,
                            std::vector<InputLocation>* output) {
  output->clear();

  InputLocation global;
  if (Err err = ParseGlobalInputLocation(location, input, &global); err.has_error())
    return err;

  if (optional_process_symbols && global.type == InputLocation::Type::kName)
    *output = GetIdentifierMatchesOnThis(optional_process_symbols, location, global.name);

  // The global one always goes last so the most specific ones come first.
  output->push_back(std::move(global));
  return Err();
}

Err ParseLocalInputLocation(const Frame* optional_frame, const std::string& input,
                            std::vector<InputLocation>* output) {
  const ProcessSymbols* process_symbols = nullptr;
  Location location;
  if (optional_frame) {
    process_symbols = optional_frame->GetThread()->GetProcess()->GetSymbols();
    location = optional_frame->GetLocation();
  }
  return ParseLocalInputLocation(process_symbols, location, input, output);
}

void EvalLocalInputLocation(
    const fxl::RefPtr<EvalContext>& eval_context, const Frame* optional_frame,
    const std::string& input,
    fit::callback<void(ErrOr<std::vector<InputLocation>>, std::optional<uint32_t> size)> cb) {
  Location cur_location;
  if (optional_frame)
    cur_location = optional_frame->GetLocation();
  return EvalLocalInputLocation(eval_context, cur_location, input, std::move(cb));
}

void EvalLocalInputLocation(
    const fxl::RefPtr<EvalContext>& eval_context, const Location& location,
    const std::string& input,
    fit::callback<void(ErrOr<std::vector<InputLocation>>, std::optional<uint32_t> size)> cb) {
  EvalGlobalInputLocation(
      eval_context, location, input,
      [eval_context, location, cb = std::move(cb)](ErrOr<InputLocation> global_location,
                                                   std::optional<uint32_t> size) mutable {
        if (global_location.has_error())
          return cb(global_location.err(), std::nullopt);

        // Possibly null.
        const ProcessSymbols* process_symbols = eval_context->GetProcessSymbols();

        std::vector<InputLocation> result;
        if (process_symbols && global_location.value().type == InputLocation::Type::kName) {
          result =
              GetIdentifierMatchesOnThis(process_symbols, location, global_location.value().name);
        }

        // The global one always goes last so the most specific ones come first.
        result.push_back(global_location.take_value());

        cb(std::move(result), size);
      });
}

Err ResolveInputLocations(const ProcessSymbols* process_symbols, const Location& location,
                          const std::string& input, bool symbolize, std::vector<Location>* output) {
  std::vector<InputLocation> input_locations;
  if (Err err = ParseLocalInputLocation(process_symbols, location, input, &input_locations);
      err.has_error()) {
    return err;
  }
  return ResolveInputLocations(process_symbols, input_locations, symbolize, output);
}

Err ResolveInputLocations(const Frame* optional_frame, const std::string& input, bool symbolize,
                          std::vector<Location>* output) {
  const ProcessSymbols* process_symbols = nullptr;
  Location location;
  if (optional_frame) {
    process_symbols = optional_frame->GetThread()->GetProcess()->GetSymbols();
    location = optional_frame->GetLocation();
  }
  return ResolveInputLocations(process_symbols, location, input, symbolize, output);
}

Err ResolveInputLocations(const ProcessSymbols* process_symbols,
                          const std::vector<InputLocation>& input_locations, bool symbolize,
                          std::vector<Location>* locations) {
  ResolveOptions options;
  options.symbolize = symbolize;

  *locations = ResolvePermissiveInputLocations(process_symbols, options,
                                               FindNameContext(process_symbols), input_locations);

  if (locations->empty()) {
    if (input_locations.size() == 1) {
      return Err("Nothing matching this %s was found.",
                 InputLocation::TypeToString(input_locations[0].type));
    }
    return Err("Nothing matching this location was found.");
  }
  return Err();
}

Err ResolveUniqueInputLocation(const ProcessSymbols* process_symbols,
                               const InputLocation& input_location, bool symbolize,
                               Location* location) {
  return ResolveUniqueInputLocation(process_symbols, std::vector<InputLocation>{input_location},
                                    symbolize, location);
}

// This implementation isn't great, it doesn't always show the best disambiguations for the given
// input.
//
// Also it misses a file name edge case: If there is one file whose full path in the symbols is a
// right-side subset of another (say "foo/bar.cc" and "something/foo/bar.cc"), then "foo/bar.cc" is
// the most unique name of the first file. But if the user types that, they'll get both matches and
// this function will report an ambiguous location.
//
// Instead, if the input is a file name and there is only one result where the file name matches
// exactly, we should pick it.
Err ResolveUniqueInputLocation(const ProcessSymbols* process_symbols,
                               const std::vector<InputLocation>& input_location, bool symbolize,
                               Location* location) {
  std::vector<Location> locations;
  Err err = ResolveInputLocations(process_symbols, input_location, symbolize, &locations);
  if (err.has_error())
    return err;

  FX_DCHECK(!locations.empty());  // Non-empty on success should be guaranteed.

  if (locations.size() == 1u) {
    // Success, got a unique location.
    *location = locations[0];
    return Err();
  }

  // When there is more than one, generate an error that lists the possibilities for disambiguation.
  std::string err_str = "This resolves to more than one location. Could be:\n";
  constexpr size_t kMaxSuggestions = 10u;

  if (!symbolize) {
    // The original call did not request symbolization which will produce very
    // non-helpful suggestions. We're not concerned about performance in this error case so re-query
    // to get the full symbols.
    locations.clear();
    ResolveInputLocations(process_symbols, input_location, true, &locations);
  }

  for (size_t i = 0; i < locations.size() && i < kMaxSuggestions; i++) {
    // Always show the full path (omit TargetSymbols) since we're doing disambiguation and the
    // problem could have been two files with the same name but different paths.
    err_str += fxl::StringPrintf(" %s ", GetBullet().c_str());
    if (locations[i].file_line().is_valid()) {
      err_str += FormatFileLine(locations[i].file_line()).AsString();
      err_str += fxl::StringPrintf(" = 0x%" PRIx64, locations[i].address());
    } else {
      FormatLocationOptions opts;
      opts.always_show_addresses = true;
      err_str += FormatLocation(locations[i], opts).AsString();
    }
    err_str += "\n";
  }
  if (locations.size() > kMaxSuggestions) {
    err_str += fxl::StringPrintf("...%zu more omitted...\n", locations.size() - kMaxSuggestions);
  }
  return Err(err_str);
}

Err ResolveUniqueInputLocation(const ProcessSymbols* process_symbols, const Location& location,
                               const std::string& input, bool symbolize, Location* output) {
  std::vector<InputLocation> input_locations;
  if (Err err = ParseLocalInputLocation(process_symbols, location, input, &input_locations);
      err.has_error()) {
    return err;
  }
  return ResolveUniqueInputLocation(process_symbols, input_locations, symbolize, output);
}

Err ResolveUniqueInputLocation(const Frame* optional_frame, const std::string& input,
                               bool symbolize, Location* output) {
  const ProcessSymbols* process_symbols = nullptr;
  Location location;
  if (optional_frame) {
    process_symbols = optional_frame->GetThread()->GetProcess()->GetSymbols();
    location = optional_frame->GetLocation();
  }

  return ResolveUniqueInputLocation(process_symbols, location, input, symbolize, output);
}

void CompleteInputLocation(const Command& command, const std::string& prefix,
                           std::vector<std::string>* completions) {
  if (!command.target())
    return;

  // Number of items of each category that can be added to the completions.
  constexpr size_t kMaxFileNames = 32;
  constexpr size_t kMaxNamespaces = 8;
  constexpr size_t kMaxClasses = 32;
  constexpr size_t kMaxFunctions = 32;

  // Extract the current code block if possible. This will be used to find local variables and to
  // prioritize symbols from the current module.
  const CodeBlock* code_block = nullptr;
  SymbolContext symbol_context = SymbolContext::ForRelativeAddresses();
  if (const Frame* frame = command.frame()) {
    const Location& location = frame->GetLocation();
    if (const CodeBlock* fn_block = location.symbol().Get()->As<CodeBlock>()) {
      symbol_context = location.symbol_context();
      code_block = fn_block->GetMostSpecificChild(symbol_context, location.address());
    }
  }

  // TODO(brettw) prioritize the current module when it's known (when there is a current frame with
  // symbol information). Factor prioritization code from find_name.cc
  for (const ModuleSymbols* mod : command.target()->GetSymbols()->GetModuleSymbols()) {
    const Index& index = mod->GetIndex();
    auto files = index.FindFilePrefixes(prefix);

    // Files get colons at the end for the user to type a line number next.
    for (auto& file : files)
      file.push_back(':');

    completions->insert(completions->end(), files.begin(), files.end());
  }

  std::sort(completions->begin(), completions->end());
  if (completions->size() > kMaxFileNames)
    completions->resize(kMaxFileNames);

  // Now search for functions matching the given input.
  FindNameOptions options(FindNameOptions::kNoKinds);
  options.how = FindNameOptions::kPrefix;

  ParsedIdentifier prefix_identifier;
  Err err = ExprParser::ParseIdentifier(prefix, &prefix_identifier);
  if (err.has_error())
    return;  // Can't match identifier names.

  // When there's a live process there is more context to find stuff.
  std::unique_ptr<FindNameContext> find_context;
  if (Process* process = command.target()->GetProcess()) {
    find_context =
        std::make_unique<FindNameContext>(process->GetSymbols(), symbol_context, code_block);
  } else {
    find_context = std::make_unique<FindNameContext>(command.target()->GetSymbols());
  }

  // First start with namespaces.
  options.find_namespaces = true;
  options.max_results = kMaxNamespaces;
  std::vector<FoundName> found_names;
  FindName(*find_context, options, prefix_identifier, &found_names);
  for (const FoundName& found : found_names)
    completions->push_back(found.GetName().GetFullName() + "::");
  options.find_namespaces = false;

  // Follow with types. Only do structure and class types since we're really looking for function
  // names. In the future it might be nice to check if there are any member functions in the types
  // before adding them.
  options.find_types = true;
  options.max_results = kMaxClasses;
  found_names.clear();
  FindName(*find_context, options, prefix_identifier, &found_names);
  for (const FoundName& found : found_names) {
    FX_DCHECK(found.kind() == zxdb::FoundName::kType);
    if (const Collection* collection = found.type()->As<Collection>())
      completions->push_back(found.GetName().GetFullName() + "::");
  }
  options.find_types = false;

  // Finish with functions.
  options.find_functions = true;
  options.max_results = kMaxFunctions;
  found_names.clear();
  FindName(*find_context, options, prefix_identifier, &found_names);
  for (const FoundName& found : found_names) {
    // When completing names, globally qualify the names to prevent ambiguity.
    completions->push_back(found.function()->GetIdentifier().GetFullName());
  }
  options.find_functions = false;
}

}  // namespace zxdb

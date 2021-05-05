// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_CONSOLE_FORMAT_NAME_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_CONSOLE_FORMAT_NAME_H_

#include "src/developer/debug/zxdb/console/output_buffer.h"
#include "src/developer/debug/zxdb/expr/parsed_identifier.h"

namespace zxdb {

class Function;
class TargetSymbols;

struct FormatIdentifierOptions {
  // Includes the preceeding "::" if the identifier is marked as globally qualified. Otherwise the
  // leading "::" is omitted regardless of qualification.
  bool show_global_qual = false;

  // When true, nonempty template parameters will be replaced with "...". This is useful in
  // relatively nonambiguous situations where long templates can get in the way.
  bool elide_templates = false;

  // The last component of function name is normally the most important, and with long templates
  // it can be difficult to find. Setting this marks that bold.
  bool bold_last = false;
};

// Formats the given string as an identifier, with any template annotations dimmed.
//
// If |show_global_qual| is set, the output will be preceeded with "::" if it's globally qualified.
// Otherwise, global qualifications on the identifier will be ignored. Callers may want to ignore
// the global qualifiers in cases where it's clear in context whether the name is global or not.
// For example, function names in backtraces are always global and adding "::" to everything adds
// noise.
//
// If |bold_last| is set, the last identifier component will be bolded. This is best when printing
// function names.
OutputBuffer FormatIdentifier(const Identifier& str, const FormatIdentifierOptions& options);
OutputBuffer FormatIdentifier(const ParsedIdentifier& str, const FormatIdentifierOptions& options);

struct FormatFunctionNameOptions {
  // Options for name formatting.
  FormatIdentifierOptions name;

  enum Params {
    kNoParams,     // No parens or parameters after the name.
    kElideParams,  // Either "()" for no params, or "(...)" if there are params.
    kParamTypes,   // Include parameter types.
  };
  Params params = Params::kParamTypes;
};

// Formats the function name with syntax highlighting.
//
// It will apply some simple rewrite rules to clean up some symbols.
//
// If optional_target_symbols is provided it can provide extra cleanup for some generated lambda
// names by using the shortest possible unique file name.
OutputBuffer FormatFunctionName(const Function* function, const FormatFunctionNameOptions& options);

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_CONSOLE_FORMAT_NAME_H_

// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/console/format_name.h"

#include <ctype.h>

#include "src/developer/debug/zxdb/common/string_util.h"
#include "src/developer/debug/zxdb/console/command_utils.h"
#include "src/developer/debug/zxdb/symbols/collection.h"
#include "src/developer/debug/zxdb/symbols/function.h"
#include "src/developer/debug/zxdb/symbols/variable.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace zxdb {

namespace {

// Checks if the function is a Clang-style lambda and formats it to the output. Returns true if
// there was a match, false means it wasn't a lambda.
//
// Clang lambdas are implemented as functions called "operator()" as a member of an unnamed class.
// GCC lambdas are also "operator()" but on a structure named like "<lambda(int)>" where the <...>
// lists the parameter types to the functions..
bool FormatClangLambda(const Function* function, OutputBuffer* out) {
  if (!function->parent())
    return false;  // Not a member function.

  if (function->GetAssignedName() != "operator()")
    return false;  // Not the right function name.

  // This is currently designed assuming the file/line will be printed separately so aren't useful
  // here. The main use of function printing is as part of locations, which will append the
  // file/line after the function name.
  //
  // If this is used in contexts where the file/line isn't shown, we should add a flag and an
  // optional_target_symbols parameter to this function so we can print it as:
  //   out->Append("λ @ " + DescribeFileLine(optional_target_symbols,
  //                                         function->decl_line()));
  // so users can tell where the lambda function is.
  auto parent_ref = function->parent().Get();  // Hold a ref to keep alive.
  const Collection* coll = parent_ref->AsCollection();
  if (coll && coll->tag() == DwarfTag::kClassType && coll->GetAssignedName().empty()) {
    // Clang-style.
    out->Append("λ");
    return true;
  } else if (coll && coll->tag() == DwarfTag::kStructureType &&
             StringBeginsWith(coll->GetAssignedName(), "<lambda(")) {
    // GCC-style.
    out->Append("λ");
    return true;
  }
  return false;
}

bool FormatRustClosure(const Function* function, OutputBuffer* out) {
  // Rust closures currently look like
  // "fuchsia_async::executor::{{impl}}::run_singlethreaded::{{closure}}<()>"
  // The function "assigned name" will be just the last component.
  if (!StringBeginsWith(function->GetAssignedName(), "{{closure}}"))
    return false;

  // As with the Clang lambda above, this assumes the file/line or function enclosing the original
  // lambda is redundant.
  out->Append("λ");
  return true;
}

// Checks to see if this identifier component will parse by itself using the expression parser
// (returns false) or if it needs escaping to be identified as an identifier (returns true).
bool IdentifierComponentNeedsEscaping(const std::string& comp) {
  if (comp.empty())
    return true;  // Empty identifier components need escaping just to identify them.
  if (isdigit(comp[0]))
    return true;  // Starting with a number implies escaping.

  for (char c : comp) {
    if (!isalnum(c) && c != '_')
      return true;
  }
  return false;
}

// Escapes the given identifier component. This does not put the "$(...) around it, but handles the
// stuff in the middle. There are two things that need to happen: handling parens and escaping
// backslashes.
//
// If parens are balanced inside the component (there is a closing paren for each opening one), they
// do not need escaping. In most cases, these will be balanced because it's some kind of textual
// representation for a function call or something. The parser can handle this and it's much more
// readable.
//
// The algorithm is to assume the parens are balanced and just escape backslashes. If it finds the
// parens are unbalanced, it recursively calls itself with paren escaping forced on. We don't try
// to be intelligent about only escaping the unbalanced parens, they all get escaped in this mode.
std::string EscapeComponent(const std::string& name, bool force_escape_parens = false) {
  std::string output;
  int paren_nesting = 0;

  for (char c : name) {
    if (c == '(') {
      if (force_escape_parens) {
        output.push_back('\\');
        output.push_back('(');
      } else {
        paren_nesting++;
        output.push_back('(');
      }
    } else if (c == ')') {
      if (force_escape_parens) {
        output.push_back('\\');
        output.push_back(')');
      } else if (paren_nesting == 0) {
        // Lone ')' without an opening paren.
        return EscapeComponent(name, true);
      } else {
        // Found closing paren for a previous opening one.
        paren_nesting--;
        output.push_back(')');
      }
    } else if (c == '$' || c == '\\') {
      // Always escape these.
      output.push_back('\\');
      output.push_back(c);
    } else {
      // Everything else is a literal.
      output.push_back(c);
    }
  }

  if (paren_nesting > 0) {
    // There was an opening paren with no closing paren.
    return EscapeComponent(name, true);
  }

  return output;
}

}  // namespace

OutputBuffer FormatFunctionName(const Function* function,
                                const FormatFunctionNameOptions& options) {
  OutputBuffer result;
  if (!FormatClangLambda(function, &result) && !FormatRustClosure(function, &result))
    result = FormatIdentifier(function->GetIdentifier(), options.name);

  const auto& params = function->parameters();
  switch (options.params) {
    case FormatFunctionNameOptions::kNoParams: {
      break;
    }
    case FormatFunctionNameOptions::kElideParams: {
      if (params.empty())
        result.Append(Syntax::kComment, "()");
      else
        result.Append(Syntax::kComment, "(…)");
      break;
    }
    case FormatFunctionNameOptions::kParamTypes: {
      std::string params_str;
      params_str.push_back('(');
      for (size_t i = 0; i < params.size(); i++) {
        if (i > 0)
          params_str += ", ";
        if (const Variable* var = params[i].Get()->AsVariable())
          params_str += var->type().Get()->GetFullName();
      }
      params_str.push_back(')');
      result.Append(Syntax::kComment, std::move(params_str));
      break;
    }
  }

  return result;
}

OutputBuffer FormatIdentifier(const Identifier& identifier,
                              const FormatIdentifierOptions& options) {
  return FormatIdentifier(ToParsedIdentifier(identifier), options);
}

// This annoyingly duplicates Identifier::GetName but is required to get syntax highlighting for all
// the components.
OutputBuffer FormatIdentifier(const ParsedIdentifier& identifier,
                              const FormatIdentifierOptions& options) {
  OutputBuffer result;
  if (options.show_global_qual && identifier.qualification() == IdentifierQualification::kGlobal)
    result.Append(identifier.GetSeparator());

  const auto& comps = identifier.components();
  for (size_t i = 0; i < comps.size(); i++) {
    const auto& comp = comps[i];
    if (i > 0)
      result.Append(identifier.GetSeparator());

    if (comp.special() == SpecialIdentifier::kNone) {
      // Name.
      std::string name = comp.name();
      if (name.empty()) {
        // Provide names for anonymous components.
        result.Append(Syntax::kComment, kAnonIdentifierComponentName);
      } else {
        bool needs_escaping = IdentifierComponentNeedsEscaping(name);
        if (needs_escaping)
          result.Append(Syntax::kComment, "$(");

        if (options.bold_last && i == comps.size() - 1)
          result.Append(Syntax::kHeading, needs_escaping ? EscapeComponent(name) : name);
        else
          result.Append(Syntax::kNormal, needs_escaping ? EscapeComponent(name) : name);

        if (needs_escaping)
          result.Append(Syntax::kComment, ")");
      }
    } else if (comp.special() == SpecialIdentifier::kAnon) {
      // Always dim anonymous names.
      result.Append(Syntax::kComment, std::string(SpecialIdentifierToString(comp.special())));
    } else {
      // Other special name.
      if (SpecialIdentifierHasData(comp.special())) {
        // Special identifier has data, dim the tag part to emphasize the contents.
        result.Append(Syntax::kComment,
                      std::string(SpecialIdentifierToString(comp.special())) + "(");
        result.Append(comp.name());
        result.Append(Syntax::kComment, ")");
      } else {
        // Standalone special identifier, just append it as normal.
        result.Append(Syntax::kNormal, std::string(SpecialIdentifierToString(comp.special())));
      }
    }

    // Template.
    if (comp.has_template()) {
      std::string t_string("<");

      if (!comp.template_contents().empty()) {
        if (options.elide_templates) {
          t_string += "…";
        } else {
          for (size_t t_i = 0; t_i < comp.template_contents().size(); t_i++) {
            if (t_i > 0)
              t_string += ", ";
            t_string += comp.template_contents()[t_i];
          }
        }
      }

      t_string.push_back('>');
      result.Append(Syntax::kComment, std::move(t_string));
    }
  }

  return result;
}

}  // namespace zxdb

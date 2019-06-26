// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/console/format_function.h"

#include "src/developer/debug/zxdb/common/string_util.h"
#include "src/developer/debug/zxdb/console/command_utils.h"
#include "src/developer/debug/zxdb/symbols/collection.h"
#include "src/developer/debug/zxdb/symbols/function.h"
#include "src/developer/debug/zxdb/symbols/variable.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace zxdb {

namespace {

// Checks if the function is a Clang-style lambda and formats it to the
// output. Returns true if there was a match, false means id wasn't a lambda.
//
// Clang lambdas are implemented as functions called "operator()" as a member
// of an unnamed class. GCC lambdas are also "operator()" but on a structure
// named like "<lambda(int)>" where the <...> lists the parameter types to the
// functions..
bool FormatClangLambda(const Function* function, OutputBuffer* out) {
  if (!function->parent())
    return false;  // Not a member function.

  if (function->GetAssignedName() != "operator()")
    return false;  // Not the right function name.

  // This is currently designed assuming the file/line will be printed
  // separately so aren't useful here. The main use of function printing is as
  // part of locations, which will append the file/line after the function
  // name.
  //
  // If this is used in contexts where the file/line isn't shown, we should add
  // a flag and an optional_target_symbols parameter to this function so we can
  // print it as:
  //   out->Append("λ @ " + DescribeFileLine(optional_target_symbols,
  //                                         function->decl_line()));
  // so users can tell where the lambda function is.
  const Collection* coll = function->parent().Get()->AsCollection();
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

  // As with the Clang lambda above, this assumes the file/line or function
  // enclosing the original lambda is redundant.
  out->Append("λ");
  return true;
}

}  // namespace

OutputBuffer FormatFunctionName(const Function* function, bool show_params) {
  OutputBuffer result;
  if (!FormatClangLambda(function, &result) && !FormatRustClosure(function, &result))
    result = FormatIdentifier(function->GetIdentifier(), true);

  const auto& params = function->parameters();
  std::string params_str;
  if (show_params) {
    params_str.push_back('(');
    for (size_t i = 0; i < params.size(); i++) {
      if (i > 0)
        params_str += ", ";
      if (const Variable* var = params[i].Get()->AsVariable())
        params_str += var->type().Get()->GetFullName();
    }
    params_str.push_back(')');
  } else {
    if (params.empty())
      params_str += "()";
    else
      params_str += "(…)";
  }

  result.Append(Syntax::kComment, std::move(params_str));
  return result;
}

}  // namespace zxdb

// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_FIDL_FIDLC_INCLUDE_FIDL_TEMPLATE_STRING_H_
#define TOOLS_FIDL_FIDLC_INCLUDE_FIDL_TEMPLATE_STRING_H_

#include <map>
#include <string>
#include <utility>
#include <variant>

namespace fidl {

// TODO(fxbug.dev/70247): Delete this
// The "substitution list" is supposed to be a string:string map.  However, for the purposes of
// conversion, we need to ensure that the substituted value is unique, even relative to similarly
// named strings in the file.  For example, consider:
//
//   const string ${TEST} = "Foo";
//
// If we substitute `Foo` for ${TEST} prior to conversion, everything is fine, as we can run the
// Unsubstitute() function below to "re-templatize" the converted file.  But what if we had inserted
// `Foo` instead?  Since we are doing simple string search and replace when re-templatizing the
// converted file, we would get the following:
//
//   const ${TEST} string = "${TEST}"
//
// The solution is to include a random string of characters along with the substitution value.  When
// the substitution is performed, this random string is appended, ensuring uniqueness, and avoiding
// the name collision described above.
using SubstitutionKey = std::string;
struct SubstitutionWithRandom {
  std::string value;
  std::string random;
};
using SubstitutionValue = std::variant<std::string, SubstitutionWithRandom>;
using Substitutions = std::map<SubstitutionKey, SubstitutionValue>;

// Holds a string with named variables to replace, using a dictionary of
// key/value string pairs (map<std::string,std::string>). The variable format is
// $name or ${name}. Variables that without a matching key in the substitution
// map are left unchanged. "$$" converts to "$" without interpreting either "$"
// as part of a variable name.
//
// Example:
//   TemplateString ts("change '${ORIGINAL}' to '${REPLACEMENT}'");
//   std::string value = ts.Substitute({
//       {"ORIGINAL", prefix},
//       {"REPLACEMENT", replacement},
//   });
class TemplateString {
 public:
  explicit TemplateString(std::string str) : str_(std::move(str)) {}

  // Constructs an empty template.
  TemplateString() : TemplateString("") {}

  // Returns true if the template string is not empty.
  explicit operator bool() const { return !str_.empty(); }

  // Returns the string value after replacing all matched variables in the
  // template string with the values for the matching keys.
  // If |remove_unmatched| is true, variables without matching keys are
  // removed from the string.
  std::string Substitute(Substitutions substitutions, bool remove_unmatched, bool with_randomized = false) const;

  // Returns the string value after replacing all matched variables in the
  // template string with the values for the matching keys.
  // Variables without matching keys are left in place.
  std::string Substitute(Substitutions substitutions) const {
    return Substitute(std::move(substitutions), false);
  }

  // TODO(fxbug.dev/70247): Delete this Perform the same substitutions as described above, but make
  // sure to include the random suffixes for each value being substituted, which is only necessary
  // during testing, when converting an old syntax source_template_ into a new syntax
  // converted_template_.  This will prevent name collision with non-templated variables identical
  // to substitution values when Unsubstitute() is run below.
  std::string SubstituteWithRandomized(Substitutions substitutions) const {
    return Substitute(std::move(substitutions), false, true);
  }

  // TODO(fxbug.dev/70247): Delete this
  // Takes a file that has had its template keys replaced with randomly suffixed versions of their
  // values, via the SubstituteWithRandomized() function above, then converted into the new
  // syntax, and re-inserts the template keys in place of those values.  The result is a new
  // template, identical to the old syntax semantically, but written in the new syntax.  Thus, an
  // old syntax template like:
  //
  //   struct ${TEST} {};
  //
  // Becomes:
  //
  //   type ${TEST} = struct {};
  //
  // This function should only be used during testing.
  static TemplateString Unsubstitute(std::string& input, const Substitutions& substitutions);

  // Returns the template string with unreplaced variables (as given at
  // construction).
  inline const std::string& str() const { return str_; }

 private:
  std::string str_;
};

}  // namespace fidl

#endif  // TOOLS_FIDL_FIDLC_INCLUDE_FIDL_TEMPLATE_STRING_H_

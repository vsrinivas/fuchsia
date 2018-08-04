// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/url/url_util.h"

#include <string.h>
#include <vector>

#include "lib/fxl/logging.h"
#include "lib/fxl/strings/ascii.h"
#include "lib/fxl/strings/string_view.h"
#include "lib/url/url_canon_internal.h"
#include "lib/url/url_file.h"
#include "lib/url/url_util_internal.h"

namespace url {

namespace {

const size_t kNumStandardURLSchemes = 7;
const char* kStandardURLSchemes[kNumStandardURLSchemes] = {
    // clang-format off
    kHttpScheme,
    kHttpsScheme,
    kFileScheme,  // Yes, file URLs can have a hostname!
    kFtpScheme,
    kGopherScheme,
    kWsScheme,    // WebSocket.
    kWssScheme,   // WebSocket secure.
    // clang-format on
};

// List of the currently installed standard schemes. This list is lazily
// initialized by InitStandardSchemes and is leaked on shutdown to prevent
// any destructors from being called that will slow us down or cause problems.
std::vector<const char*>* standard_schemes = NULL;

// See the LockStandardSchemes declaration in the header.
bool standard_schemes_locked = false;

// Ensures that the standard_schemes list is initialized, does nothing if it
// already has values.
void InitStandardSchemes() {
  if (standard_schemes)
    return;
  standard_schemes = new std::vector<const char*>;
  for (size_t i = 0; i < kNumStandardURLSchemes; i++)
    standard_schemes->push_back(kStandardURLSchemes[i]);
}

// Given a string and a range inside the string, compares it to the given
// lower-case |compare_to| buffer.
inline bool DoCompareSchemeComponent(const char* spec,
                                     const Component& component,
                                     const char* compare_to) {
  if (component.is_invalid_or_empty())
    return compare_to[0] == 0;  // When component is empty, match empty scheme.
  return LowerCaseEqualsASCII(
      fxl::StringView(&spec[component.begin], component.len()),
      fxl::StringView(compare_to));
}

}  // namespace

// Returns true if the given scheme identified by |scheme| within |spec| is one
// of the registered "standard" schemes.
bool IsStandard(const char* spec, const Component& scheme) {
  if (scheme.is_invalid_or_empty())
    return false;  // Empty or invalid schemes are non-standard.

  InitStandardSchemes();
  for (size_t i = 0; i < standard_schemes->size(); i++) {
    if (LowerCaseEqualsASCII(fxl::StringView(&spec[scheme.begin], scheme.len()),
                             fxl::StringView(standard_schemes->at(i))))
      return true;
  }
  return false;
}

bool FindAndCompareScheme(const char* str, size_t str_len, const char* compare,
                          Component* found_scheme) {
  // Before extracting scheme, canonicalize the URL to remove any whitespace.
  // This matches the canonicalization done in Canonicalize function.
  RawCanonOutputT<char> whitespace_buffer;
  size_t spec_len;
  const char* spec =
      RemoveURLWhitespace(str, str_len, &whitespace_buffer, &spec_len);

  Component our_scheme;
  if (!ExtractScheme(spec, spec_len, &our_scheme)) {
    // No scheme.
    if (found_scheme)
      *found_scheme = Component();
    return false;
  }
  if (found_scheme)
    *found_scheme = our_scheme;
  return DoCompareSchemeComponent(spec, our_scheme, compare);
}

bool Canonicalize(const char* in_spec, size_t in_spec_len, bool trim_path_end,
                  CharsetConverter* charset_converter, CanonOutput* output,
                  Parsed* output_parsed) {
  // Remove any whitespace from the middle of the relative URL, possibly
  // copying to the new buffer.
  RawCanonOutputT<char> whitespace_buffer;
  size_t spec_len;
  const char* spec =
      RemoveURLWhitespace(in_spec, in_spec_len, &whitespace_buffer, &spec_len);

  Parsed parsed_input;

  Component scheme;
  if (!ExtractScheme(spec, spec_len, &scheme))
    return false;

  // This is the parsed version of the input URL, we have to canonicalize it
  // before storing it in our object.
  bool success;
  if (DoCompareSchemeComponent(spec, scheme, url::kFileScheme)) {
    // File URLs are special.
    ParseFileURL(spec, spec_len, &parsed_input);
    success = CanonicalizeFileURL(spec, spec_len, parsed_input,
                                  charset_converter, output, output_parsed);
  } else if (IsStandard(spec, scheme)) {
    // All "normal" URLs.
    ParseStandardURL(spec, spec_len, &parsed_input);
    success = CanonicalizeStandardURL(spec, spec_len, parsed_input,
                                      charset_converter, output, output_parsed);

  } else if (DoCompareSchemeComponent(spec, scheme, url::kMailToScheme)) {
    // Mailto URLs are treated like standard URLs, with only a scheme, path,
    // and query.
    ParseMailtoURL(spec, spec_len, &parsed_input);
    success = CanonicalizeMailtoURL(spec, spec_len, parsed_input, output,
                                    output_parsed);

  } else {
    // "Weird" URLs like data: and javascript:.
    ParsePathURL(spec, spec_len, trim_path_end, &parsed_input);
    success = CanonicalizePathURL(spec, spec_len, parsed_input, output,
                                  output_parsed);
  }
  return success;
}

bool ResolveRelative(const char* base_spec, size_t base_spec_len,
                     const Parsed& base_parsed, const char* in_relative,
                     size_t in_relative_length,
                     CharsetConverter* charset_converter, CanonOutput* output,
                     Parsed* output_parsed) {
  // Remove any whitespace from the middle of the relative URL, possibly
  // copying to the new buffer.
  RawCanonOutputT<char> whitespace_buffer;
  size_t relative_length;
  const char* relative = RemoveURLWhitespace(
      in_relative, in_relative_length, &whitespace_buffer, &relative_length);
  bool base_is_authority_based = false;
  bool base_is_hierarchical = false;
  if (base_spec && base_parsed.scheme.is_nonempty()) {
    size_t after_scheme = base_parsed.scheme.end() + 1;  // Skip past the colon.
    size_t num_slashes =
        CountConsecutiveSlashes(base_spec, after_scheme, base_spec_len);
    base_is_authority_based = num_slashes > 1;
    base_is_hierarchical = num_slashes > 0;
  }

  bool standard_base_scheme = base_parsed.scheme.is_nonempty() &&
                              IsStandard(base_spec, base_parsed.scheme);

  bool is_relative;
  Component relative_component;
  if (!IsRelativeURL(base_spec, base_parsed, relative, relative_length,
                     (base_is_hierarchical || standard_base_scheme),
                     &is_relative, &relative_component)) {
    // Error resolving.
    return false;
  }

  // Pretend for a moment that |base_spec| is a standard URL. Normally
  // non-standard URLs are treated as PathURLs, but if the base has an
  // authority we would like to preserve it.
  if (is_relative && base_is_authority_based && !standard_base_scheme) {
    Parsed base_parsed_authority;
    ParseStandardURL(base_spec, base_spec_len, &base_parsed_authority);
    if (base_parsed_authority.host.is_nonempty()) {
      RawCanonOutputT<char> temporary_output;
      bool did_resolve_succeed = ResolveRelativeURL(
          base_spec, base_parsed_authority, false, relative, relative_component,
          charset_converter, &temporary_output, output_parsed);
      // The output_parsed is incorrect at this point (because it was built
      // based on base_parsed_authority instead of base_parsed) and needs to be
      // re-created.
      Canonicalize(temporary_output.data(), temporary_output.length(), true,
                   charset_converter, output, output_parsed);
      return did_resolve_succeed;
    }
  } else if (is_relative) {
    // Relative, resolve and canonicalize.
    bool file_base_scheme =
        base_parsed.scheme.is_nonempty() &&
        DoCompareSchemeComponent(base_spec, base_parsed.scheme, kFileScheme);
    return ResolveRelativeURL(base_spec, base_parsed, file_base_scheme,
                              relative, relative_component, charset_converter,
                              output, output_parsed);
  }

  // Not relative, canonicalize the input.
  return Canonicalize(relative, relative_length, true, charset_converter,
                      output, output_parsed);
}

void Initialize() { InitStandardSchemes(); }

void Shutdown() {
  if (standard_schemes) {
    delete standard_schemes;
    standard_schemes = NULL;
  }
}

void AddStandardScheme(const char* new_scheme) {
  // If this assert triggers, it means you've called AddStandardScheme after
  // LockStandardSchemes have been called (see the header file for
  // LockStandardSchemes for more).
  //
  // This normally means you're trying to set up a new standard scheme too late
  // in your application's init process. Locate where your app does this
  // initialization and calls LockStandardSchemes, and add your new standard
  // scheme there.
  FXL_DCHECK(!standard_schemes_locked)
      << "Trying to add a standard scheme after the list has been locked.";

  size_t scheme_len = strlen(new_scheme);
  if (scheme_len == 0)
    return;

  // Duplicate the scheme into a new buffer and add it to the list of standard
  // schemes. This pointer will be leaked on shutdown.
  char* dup_scheme = new char[scheme_len + 1];
  // ANNOTATE_LEAKING_OBJECT_PTR(dup_scheme);
  memcpy(dup_scheme, new_scheme, scheme_len + 1);

  InitStandardSchemes();
  standard_schemes->push_back(dup_scheme);
}

void LockStandardSchemes() { standard_schemes_locked = true; }

void EncodeURIComponent(const char* input, size_t length, CanonOutput* output) {
  for (size_t i = 0; i < length; ++i) {
    unsigned char c = static_cast<unsigned char>(input[i]);
    if (IsComponentChar(c))
      output->push_back(c);
    else
      AppendEscapedChar(c, output);
  }
}

}  // namespace url

// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_URL_URL_UTIL_H_
#define SRC_LIB_URL_URL_UTIL_H_

#include <string>

#include "src/lib/url/third_party/mozilla/url_parse.h"
#include "src/lib/url/url_canon.h"
#include "src/lib/url/url_constants.h"
#include "src/lib/url/url_export.h"

namespace url {

// Schemes --------------------------------------------------------------------

// Locates the scheme in the given string and places it into |found_scheme|,
// which may be NULL to indicate the caller does not care about the range.
//
// Returns whether the given |compare| scheme matches the scheme found in the
// input (if any). The |compare| scheme must be a valid canonical scheme or
// the result of the comparison is undefined.
URL_EXPORT bool FindAndCompareScheme(const char* str, size_t str_len,
                                     const char* compare,
                                     Component* found_scheme);
inline bool FindAndCompareScheme(const std::string& str, const char* compare,
                                 Component* found_scheme) {
  return FindAndCompareScheme(str.data(), str.size(), compare, found_scheme);
}

// Returns true if the given string represents a URL whose scheme is in the list
// of known standard-format schemes (see AddStandardScheme).
URL_EXPORT bool IsStandard(const char* spec, const Component& scheme);

// URL library wrappers -------------------------------------------------------

// Parses the given spec according to the extracted scheme type. Normal users
// should use the URL object, although this may be useful if performance is
// critical and you don't want to do the heap allocation for the std::string.
//
// As with the Canonicalize* functions, the charset converter can
// be NULL to use UTF-8 (it will be faster in this case).
//
// Returns true if a valid URL was produced, false if not. On failure, the
// output and parsed structures will still be filled and will be consistent,
// but they will not represent a loadable URL.
URL_EXPORT bool Canonicalize(const char* spec, size_t spec_len,
                             bool trim_path_end,
                             CharsetConverter* charset_converter,
                             CanonOutput* output, Parsed* output_parsed);

// Resolves a potentially relative URL relative to the given parsed base URL.
// The base MUST be valid. The resulting canonical URL and parsed information
// will be placed in to the given out variables.
//
// The relative need not be relative. If we discover that it's absolute, this
// will produce a canonical version of that URL. See Canonicalize() for more
// about the charset_converter.
//
// Returns true if the output is valid, false if the input could not produce
// a valid URL.
URL_EXPORT bool ResolveRelative(const char* base_spec, size_t base_spec_len,
                                const Parsed& base_parsed, const char* relative,
                                size_t relative_length,
                                CharsetConverter* charset_converter,
                                CanonOutput* output, Parsed* output_parsed);

// String helper functions ----------------------------------------------------

// Escapes the given string as defined by the JS method encodeURIComponent. See
// https://developer.mozilla.org/en/JavaScript/Reference/Global_Objects/encodeURIComponent
URL_EXPORT void EncodeURIComponent(const char* input, size_t length,
                                   CanonOutput* output);

}  // namespace url

#endif  // SRC_LIB_URL_URL_UTIL_H_

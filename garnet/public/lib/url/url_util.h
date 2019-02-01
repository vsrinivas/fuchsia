// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_URL_URL_UTIL_H_
#define LIB_URL_URL_UTIL_H_

#include <string>

#include "lib/url/third_party/mozilla/url_parse.h"
#include "lib/url/url_canon.h"
#include "lib/url/url_constants.h"
#include "lib/url/url_export.h"

namespace url {

// Init ------------------------------------------------------------------------

// Initialization is NOT required, it will be implicitly initialized when first
// used. However, this implicit initialization is NOT threadsafe. If you are
// using this library in a threaded environment and don't have a consistent
// "first call" (an example might be calling AddStandardScheme with your special
// application-specific schemes) then you will want to call initialize before
// spawning any threads.
//
// It is OK to call this function more than once, subsequent calls will be
// no-ops, unless Shutdown was called in the mean time. This will also be a
// no-op if other calls to the library have forced an initialization beforehand.
URL_EXPORT void Initialize();

// Cleanup is not required, except some strings may leak. For most user
// applications, this is fine. If you're using it in a library that may get
// loaded and unloaded, you'll want to unload to properly clean up your
// library.
URL_EXPORT void Shutdown();

// Schemes --------------------------------------------------------------------

// Adds an application-defined scheme to the internal list of "standard-format"
// URL schemes. A standard-format scheme adheres to what RFC 3986 calls "generic
// URI syntax" (https://tools.ietf.org/html/rfc3986#section-3).
//
// This function is not threadsafe and can not be called concurrently with any
// other url_util function. It will assert if the list of standard schemes has
// been locked (see LockStandardSchemes).
URL_EXPORT void AddStandardScheme(const char* new_scheme);

// Sets a flag to prevent future calls to AddStandardScheme from succeeding.
//
// This is designed to help prevent errors for multithreaded applications.
// Normal usage would be to call AddStandardScheme for your custom schemes at
// the beginning of program initialization, and then LockStandardSchemes. This
// prevents future callers from mistakenly calling AddStandardScheme when the
// program is running with multiple threads, where such usage would be
// dangerous.
//
// We could have had AddStandardScheme use a lock instead, but that would add
// some platform-specific dependencies we don't otherwise have now, and is
// overkill considering the normal usage is so simple.
URL_EXPORT void LockStandardSchemes();

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

#endif  // LIB_URL_URL_UTIL_H_

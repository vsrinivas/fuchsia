// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Functions for canonicalizing "path" URLs. Not to be confused with the path
// of a URL, these are URLs that have no authority section, only a path. For
// example, "javascript:" and "data:".

#include "lib/url/url_canon.h"
#include "lib/url/url_canon_internal.h"

namespace url {

namespace {

// Canonicalize the given |component| from |source| into |output| and
// |new_component|. If |separator| is non-zero, it is pre-pended to |output|
// prior to the canonicalized component; i.e. for the '?' or '#' characters.
bool DoCanonicalizePathComponent(const char* source, const Component& component,
                                 char separator, CanonOutput* output,
                                 Component* new_component) {
  bool success = true;
  if (component.is_valid()) {
    if (separator)
      output->push_back(separator);
    // Copy the path using path URL's more lax escaping rules (think for
    // javascript:). We convert to UTF-8 and escape non-ASCII, but leave all
    // ASCII characters alone. This helps readability of JavaStript.
    new_component->begin = output->length();
    size_t end = component.end();
    for (size_t i = component.begin; i < end; i++) {
      unsigned char uch = static_cast<unsigned char>(source[i]);
      if (uch < 0x20 || uch >= 0x80)
        success &= AppendUTF8EscapedChar(source, &i, end, output);
      else
        output->push_back(static_cast<char>(uch));
    }
    new_component->set_len(output->length() - new_component->begin);
  } else {
    // Empty part.
    new_component->reset();
  }
  return success;
}

}  // namespace

bool CanonicalizePathURL(const char* spec, size_t spec_len,
                         const Parsed& parsed, CanonOutput* output,
                         Parsed* new_parsed) {
  URLComponentSource source(spec);

  // Scheme: this will append the colon.
  bool success = CanonicalizeScheme(source.scheme, parsed.scheme, output,
                                    &new_parsed->scheme);

  // We assume there's no authority for path URLs. Note that hosts should never
  // have -1 length.
  new_parsed->username.reset();
  new_parsed->password.reset();
  new_parsed->host.reset();
  new_parsed->port.reset();
  // We allow path URLs to have the path, query and fragment components, but we
  // will canonicalize each of the via the weaker path URL rules.
  success &= DoCanonicalizePathComponent(source.path, parsed.path, '\0', output,
                                         &new_parsed->path);
  success &= DoCanonicalizePathComponent(source.query, parsed.query, '?',
                                         output, &new_parsed->query);
  success &= DoCanonicalizePathComponent(source.ref, parsed.ref, '#', output,
                                         &new_parsed->ref);

  return success;
}

}  // namespace url

// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Functions for canonicalizing "mailto:" URLs.

#include "lib/url/url_canon.h"
#include "lib/url/url_canon_internal.h"
#include "lib/url/url_file.h"
#include "lib/url/url_parse_internal.h"

namespace url {

bool CanonicalizeMailtoURL(const char* spec, size_t spec_len,
                           const Parsed& parsed, CanonOutput* output,
                           Parsed* new_parsed) {
  URLComponentSource source(spec);
  // mailto: only uses {scheme, path, query} -- clear the rest.
  new_parsed->username = Component();
  new_parsed->password = Component();
  new_parsed->host = Component();
  new_parsed->port = Component();
  new_parsed->ref = Component();

  // Scheme (known, so we don't bother running it through the more
  // complicated scheme canonicalizer).
  new_parsed->scheme.begin = output->length();
  output->Append("mailto:", 7);
  new_parsed->scheme.set_len(6);

  bool success = true;

  // Path
  if (parsed.path.is_valid()) {
    new_parsed->path.begin = output->length();

    // Copy the path using path URL's more lax escaping rules.
    // We convert to UTF-8 and escape non-ASCII, but leave all
    // ASCII characters alone.
    size_t end = parsed.path.end();
    for (size_t i = parsed.path.begin; i < end; ++i) {
      unsigned char uch = static_cast<unsigned char>(source.path[i]);
      if (uch < 0x20 || uch >= 0x80)
        success &= AppendUTF8EscapedChar(source.path, &i, end, output);
      else
        output->push_back(static_cast<char>(uch));
    }

    new_parsed->path.set_len(output->length() - new_parsed->path.begin);
  } else {
    // No path at all
    new_parsed->path.reset();
  }

  // Query -- always use the default UTF8 charset converter.
  CanonicalizeQuery(source.query, parsed.query, NULL, output,
                    &new_parsed->query);

  return success;
}

}  // namespace url

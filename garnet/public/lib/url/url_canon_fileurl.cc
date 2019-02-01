// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Functions for canonicalizing "file:" URLs.

#include "lib/url/url_canon.h"
#include "lib/url/url_canon_internal.h"
#include "lib/url/url_file.h"
#include "lib/url/url_parse_internal.h"

namespace url {

bool FileCanonicalizePath(const char* spec, const Component& path,
                          CanonOutput* output, Component* out_path) {
  // Copies and normalizes the "c:" at the beginning, if present.
  out_path->begin = output->length();
  size_t after_drive;
  after_drive = path.begin;

  // Copies the rest of the path, starting from the slash following the
  // drive colon (if any, Windows only), or the first slash of the path.
  bool success = true;
  if (after_drive < path.end()) {
    // Use the regular path canonicalizer to canonicalize the rest of the
    // path. Give it a fake output component to write into. CanonicalizeFile
    // will compute the full path component.
    Component sub_path = MakeRange(after_drive, path.end());
    Component fake_output_path;
    success = CanonicalizePath(spec, sub_path, output, &fake_output_path);
  } else {
    // No input path, canonicalize to a slash.
    output->push_back('/');
  }

  out_path->set_len(output->length() - out_path->begin);
  return success;
}

bool CanonicalizeFileURL(const char* spec, size_t spec_len,
                         const Parsed& parsed,
                         CharsetConverter* query_converter, CanonOutput* output,
                         Parsed* new_parsed) {
  URLComponentSource source(spec);

  // Things we don't set in file: URLs.
  new_parsed->username = Component();
  new_parsed->password = Component();
  new_parsed->port = Component();

  // Scheme (known, so we don't bother running it through the more
  // complicated scheme canonicalizer).
  new_parsed->scheme.begin = output->length();
  output->Append("file://", 7);
  new_parsed->scheme.set_len(4);

  // Append the host. For many file URLs, this will be empty. For UNC, this
  // will be present.
  // TODO(brettw) This doesn't do any checking for host name validity. We
  // should probably handle validity checking of UNC hosts differently than
  // for regular IP hosts.
  bool success =
      CanonicalizeHost(source.host, parsed.host, output, &new_parsed->host);
  success &=
      FileCanonicalizePath(source.path, parsed.path, output, &new_parsed->path);
  CanonicalizeQuery(source.query, parsed.query, query_converter, output,
                    &new_parsed->query);

  // Ignore failure for refs since the URL can probably still be loaded.
  CanonicalizeRef(source.ref, parsed.ref, output, &new_parsed->ref);

  return success;
}

}  // namespace url

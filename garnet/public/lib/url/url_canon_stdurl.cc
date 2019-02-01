// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Functions to canonicalize "standard" URLs, which are ones that have an
// authority section including a host name.

#include "lib/url/url_canon.h"
#include "lib/url/url_canon_internal.h"
#include "lib/url/url_constants.h"

namespace url {

bool CanonicalizeStandardURL(const char* spec, size_t spec_len,
                             const Parsed& parsed,
                             CharsetConverter* query_converter,
                             CanonOutput* output, Parsed* new_parsed) {
  URLComponentSource source(spec);
  // Scheme: this will append the colon.
  bool success = CanonicalizeScheme(source.scheme, parsed.scheme, output,
                                    &new_parsed->scheme);

  // Authority (username, password, host, port)
  bool have_authority;
  if (parsed.username.is_valid() || parsed.password.is_valid() ||
      parsed.host.is_nonempty() || parsed.port.is_valid()) {
    have_authority = true;

    // Only write the authority separators when we have a scheme.
    if (parsed.scheme.is_valid()) {
      output->push_back('/');
      output->push_back('/');
    }

    // User info: the canonicalizer will handle the : and @.
    success &= CanonicalizeUserInfo(
        source.username, parsed.username, source.password, parsed.password,
        output, &new_parsed->username, &new_parsed->password);

    success &=
        CanonicalizeHost(source.host, parsed.host, output, &new_parsed->host);

    // Host must not be empty for standard URLs.
    if (parsed.host.is_invalid_or_empty())
      success = false;

    // Port: the port canonicalizer will handle the colon.
    int default_port = DefaultPortForScheme(
        &output->data()[new_parsed->scheme.begin], new_parsed->scheme.len());
    success &= CanonicalizePort(source.port, parsed.port, default_port, output,
                                &new_parsed->port);
  } else {
    // No authority, clear the components.
    have_authority = false;
    new_parsed->host.reset();
    new_parsed->username.reset();
    new_parsed->password.reset();
    new_parsed->port.reset();
    success = false;  // Standard URLs must have an authority.
  }

  // Path
  if (parsed.path.is_valid()) {
    success &=
        CanonicalizePath(source.path, parsed.path, output, &new_parsed->path);
  } else if (have_authority || parsed.query.is_valid() ||
             parsed.ref.is_valid()) {
    // When we have an empty path, make up a path when we have an authority
    // or something following the path. The only time we allow an empty
    // output path is when there is nothing else.
    new_parsed->path = Component(output->length(), 1);
    output->push_back('/');
  } else {
    // No path at all
    new_parsed->path.reset();
  }

  // Query
  CanonicalizeQuery(source.query, parsed.query, query_converter, output,
                    &new_parsed->query);

  // Ref: ignore failure for this, since the page can probably still be loaded.
  CanonicalizeRef(source.ref, parsed.ref, output, &new_parsed->ref);

  return success;
}

// Returns the default port for the given canonical scheme, or PORT_UNSPECIFIED
// if the scheme is unknown.
int DefaultPortForScheme(const char* scheme, size_t scheme_len) {
  int default_port = PORT_UNSPECIFIED;
  switch (scheme_len) {
    case 4:
      if (!strncmp(scheme, kHttpScheme, scheme_len))
        default_port = 80;
      break;
    case 5:
      if (!strncmp(scheme, kHttpsScheme, scheme_len))
        default_port = 443;
      break;
    case 3:
      if (!strncmp(scheme, kFtpScheme, scheme_len))
        default_port = 21;
      else if (!strncmp(scheme, kWssScheme, scheme_len))
        default_port = 443;
      break;
    case 6:
      if (!strncmp(scheme, kGopherScheme, scheme_len))
        default_port = 70;
      break;
    case 2:
      if (!strncmp(scheme, kWsScheme, scheme_len))
        default_port = 80;
      break;
  }
  return default_port;
}

}  // namespace url

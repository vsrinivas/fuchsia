// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Canonicalizer functions for working with and resolving relative URLs.

#include <iostream>

#include "lib/fxl/logging.h"
#include "lib/url/url_canon.h"
#include "lib/url/url_canon_internal.h"
#include "lib/url/url_constants.h"
#include "lib/url/url_file.h"
#include "lib/url/url_parse_internal.h"
#include "lib/url/url_util_internal.h"

namespace url {

namespace {

// Firefox does a case-sensitive compare (which is probably wrong--Mozilla bug
// 379034), whereas IE is case-insensitive.
//
// We choose to be more permissive like IE. We don't need to worry about
// unescaping or anything here: neither IE or Firefox allow this. We also
// don't have to worry about invalid scheme characters since we are comparing
// against the canonical scheme of the base.
//
// The base URL should always be canonical, therefore it should be ASCII.
bool AreSchemesEqual(const char* base, const Component& base_scheme,
                     const char* cmp, const Component& cmp_scheme) {
  if (base_scheme.len() != cmp_scheme.len())
    return false;
  for (size_t i = 0; i < base_scheme.len(); i++) {
    // We assume the base is already canonical, so we don't have to
    // canonicalize it.
    if (CanonicalSchemeChar(cmp[cmp_scheme.begin + i]) !=
        base[base_scheme.begin + i])
      return false;
  }
  return true;
}

// Copies all characters in the range [begin, end) of |spec| to the output,
// up until and including the last slash. There should be a slash in the
// range, if not, nothing will be copied.
//
// For stardard URLs the input should be canonical, but when resolving relative
// URLs on a non-standard base (like "data:") the input can be anything.
void CopyToLastSlash(const char* spec, int begin, int end,
                     CanonOutput* output) {
  // Find the last slash.
  int last_slash = -1;
  for (int i = end - 1; i >= begin; i--) {
    if (spec[i] == '/' || spec[i] == '\\') {
      last_slash = i;
      break;
    }
  }
  if (last_slash < 0)
    return;  // No slash.

  // Copy.
  for (int i = begin; i <= last_slash; i++)
    output->push_back(spec[i]);
}

// Copies a single component from the source to the output. This is used
// when resolving relative URLs and a given component is unchanged. Since the
// source should already be canonical, we don't have to do anything special,
// and the input is ASCII.
void CopyOneComponent(const char* source, const Component& source_component,
                      CanonOutput* output, Component* output_component) {
  if (!source_component.is_valid()) {
    // This component is not present.
    *output_component = Component();
    return;
  }

  output_component->begin = output->length();
  int source_end = source_component.end();
  for (int i = source_component.begin; i < source_end; i++)
    output->push_back(source[i]);
  output_component->set_len(output->length() - output_component->begin);
}

// A subroutine of DoResolveRelativeURL, this resolves the URL knowning that
// the input is a relative path or less (qyuery or ref).
bool DoResolveRelativePath(const char* base_url, const Parsed& base_parsed,
                           bool base_is_file, const char* relative_url,
                           const Component& relative_component,
                           CharsetConverter* query_converter,
                           CanonOutput* output, Parsed* out_parsed) {
  bool success = true;

  // We know the authority section didn't change, copy it to the output. We
  // also know we have a path so can copy up to there.
  Component path, query, ref;
  ParsePathInternal(relative_url, relative_component, &path, &query, &ref);
  // Canonical URLs always have a path, so we can use that offset.
  output->Append(base_url, base_parsed.path.begin);

  if (path.is_nonempty()) {
    // The path is replaced or modified.
    int true_path_begin = output->length();
    int base_path_begin = base_parsed.path.begin;

    if (IsURLSlash(relative_url[path.begin])) {
      // Easy case: the path is an absolute path on the server, so we can
      // just replace everything from the path on with the new versions.
      // Since the input should be canonical hierarchical URL, we should
      // always have a path.
      success &=
          CanonicalizePath(relative_url, path, output, &out_parsed->path);
    } else {
      // Relative path, replace the query, and reference. We take the
      // original path with the file part stripped, and append the new path.
      // The canonicalizer will take care of resolving ".." and "."
      int path_begin = output->length();
      CopyToLastSlash(base_url, base_path_begin, base_parsed.path.end(),
                      output);
      success &=
          CanonicalizePartialPath(relative_url, path, path_begin, output);
      out_parsed->path = MakeRange(path_begin, output->length());

      // Copy the rest of the stuff after the path from the relative path.
    }

    // Finish with the query and reference part (these can't fail).
    CanonicalizeQuery(relative_url, query, query_converter, output,
                      &out_parsed->query);
    CanonicalizeRef(relative_url, ref, output, &out_parsed->ref);

    // Fix the path beginning to add back the "C:" we may have written above.
    out_parsed->path = MakeRange(true_path_begin, out_parsed->path.end());
    return success;
  }

  // If we get here, the path is unchanged: copy to output.
  CopyOneComponent(base_url, base_parsed.path, output, &out_parsed->path);

  if (query.is_valid()) {
    // Just the query specified, replace the query and reference (ignore
    // failures for refs)
    CanonicalizeQuery(relative_url, query, query_converter, output,
                      &out_parsed->query);
    CanonicalizeRef(relative_url, ref, output, &out_parsed->ref);
    return success;
  }

  // If we get here, the query is unchanged: copy to output. Note that the
  // range of the query parameter doesn't include the question mark, so we
  // have to add it manually if there is a component.
  if (base_parsed.query.is_valid())
    output->push_back('?');
  CopyOneComponent(base_url, base_parsed.query, output, &out_parsed->query);

  if (ref.is_valid()) {
    // Just the reference specified: replace it (ignoring failures).
    CanonicalizeRef(relative_url, ref, output, &out_parsed->ref);
    return success;
  }

  // We should always have something to do in this function, the caller checks
  // that some component is being replaced.
  FXL_DCHECK(false) << "Not reached";
  return success;
}

// Resolves a relative URL that happens to be an absolute file path. Examples
// include: "//hostname/path", "/c:/foo", and "//hostname/c:/foo".
bool DoResolveAbsoluteFile(const char* relative_url,
                           const Component& relative_component,
                           CharsetConverter* query_converter,
                           CanonOutput* output, Parsed* out_parsed) {
  // Parse the file URL. The file URl parsing function uses the same logic
  // as we do for determining if the file is absolute, in which case it will
  // not bother to look for a scheme.
  Parsed relative_parsed;
  ParseFileURL(&relative_url[relative_component.begin],
               relative_component.len(), &relative_parsed);

  return CanonicalizeFileURL(&relative_url[relative_component.begin],
                             relative_component.len(), relative_parsed,
                             query_converter, output, out_parsed);
}

}  // namespace

// See IsRelativeURL in the header file for usage.
bool IsRelativeURL(const char* base, const Parsed& base_parsed, const char* url,
                   size_t url_len, bool is_base_hierarchical, bool* is_relative,
                   Component* relative_component) {
  *is_relative = false;  // So we can default later to not relative.

  // Trim whitespace and construct a new range for the substring.
  size_t begin = 0;
  TrimURL(url, &begin, &url_len);
  if (begin >= url_len) {
    // Empty URLs are relative, but do nothing.
    *relative_component = Component(begin, 0);
    *is_relative = true;
    return true;
  }

  // See if we've got a scheme, if not, we know this is a relative URL.
  // BUT, just because we have a scheme, doesn't make it absolute.
  // "http:foo.html" is a relative URL with path "foo.html". If the scheme is
  // empty, we treat it as relative (":foo"), like IE does.
  Component scheme;
  const bool scheme_is_empty =
      !ExtractScheme(url, url_len, &scheme) || scheme.len() == 0;
  if (scheme_is_empty) {
    if (url[begin] == '#') {
      // |url| is a bare fragment (e.g. "#foo"). This can be resolved against
      // any base. Fall-through.
    } else if (!is_base_hierarchical) {
      // Don't allow relative URLs if the base scheme doesn't support it.
      return false;
    }

    *relative_component = MakeRange(begin, url_len);
    *is_relative = true;
    return true;
  }

  // If the scheme isn't valid, then it's relative.
  int scheme_end = scheme.end();
  for (int i = scheme.begin; i < scheme_end; i++) {
    if (!CanonicalSchemeChar(url[i])) {
      if (!is_base_hierarchical) {
        // Don't allow relative URLs if the base scheme doesn't support it.
        return false;
      }
      *relative_component = MakeRange(begin, url_len);
      *is_relative = true;
      return true;
    }
  }

  // If the scheme is not the same, then we can't count it as relative.
  if (!AreSchemesEqual(base, base_parsed.scheme, url, scheme))
    return true;

  // When the scheme that they both share is not hierarchical, treat the
  // incoming scheme as absolute (this way with the base of "data:foo",
  // "data:bar" will be reported as absolute.
  if (!is_base_hierarchical)
    return true;

  size_t colon_offset = scheme.end();

  // ExtractScheme guarantees that the colon immediately follows what it
  // considers to be the scheme. CountConsecutiveSlashes will handle the
  // case where the begin offset is the end of the input.
  size_t num_slashes = CountConsecutiveSlashes(url, colon_offset + 1, url_len);

  if (num_slashes == 0 || num_slashes == 1) {
    // No slashes means it's a relative path like "http:foo.html". One slash
    // is an absolute path. "http:/home/foo.html"
    *is_relative = true;
    *relative_component = MakeRange(colon_offset + 1, url_len);
    return true;
  }

  // Two or more slashes after the scheme we treat as absolute.
  return true;
}

// TODO(brettw) treat two slashes as root like Mozilla for FTP?
bool ResolveRelativeURL(const char* base_url, const Parsed& base_parsed,
                        bool base_is_file, const char* relative_url,
                        const Component& relative_component,
                        CharsetConverter* query_converter, CanonOutput* output,
                        Parsed* out_parsed) {
  // Starting point for our output parsed. We'll fix what we change.
  *out_parsed = base_parsed;

  // Sanity check: the input should have a host or we'll break badly below.
  // We can only resolve relative URLs with base URLs that have hosts and
  // paths (even the default path of "/" is OK).
  //
  // We allow hosts with no length so we can handle file URLs, for example.
  if (base_parsed.path.is_invalid_or_empty()) {
    // On error, return the input (resolving a relative URL on a non-relative
    // base = the base).
    size_t base_len = base_parsed.Length();
    for (size_t i = 0; i < base_len; i++)
      output->push_back(base_url[i]);
    return false;
  }

  if (relative_component.is_invalid_or_empty()) {
    // Empty relative URL, leave unchanged, only removing the ref component.
    size_t base_len = base_parsed.Length();
    if (base_parsed.ref.is_valid()) {
      base_len -= base_parsed.ref.len() + 1;
    }
    out_parsed->ref.reset();
    output->Append(base_url, base_len);
    return true;
  }

  size_t num_slashes = CountConsecutiveSlashes(
      relative_url, relative_component.begin, relative_component.end());

  // Other platforms need explicit handling for file: URLs with multiple
  // slashes because the generic scheme parsing always extracts a host, but a
  // file: URL only has a host if it has exactly 2 slashes. Even if it does
  // have a host, we want to use the special host detection logic for file
  // URLs provided by DoResolveAbsoluteFile(), as opposed to the generic host
  // detection logic, for consistency with parsing file URLs from scratch.
  // This also handles the special case where the URL is only slashes,
  // since that doesn't have a host part either.
  if (base_is_file &&
      (num_slashes >= 2 || num_slashes == relative_component.len())) {
    return DoResolveAbsoluteFile(relative_url, relative_component,
                                 query_converter, output, out_parsed);
  }

  // Any other double-slashes mean that this is relative to the scheme.
  if (num_slashes >= 2) {
    // Make & parse an url with base_url's scheme and everything else from
    // relative_url.
    std::string new_url;
    new_url.reserve(base_parsed.scheme.len() + 1 + relative_component.len());
    new_url.append(&base_url[base_parsed.scheme.begin],
                   base_parsed.scheme.len());
    new_url.push_back(':');
    new_url.append(&relative_url[relative_component.begin],
                   relative_component.len());
    Parsed new_parsed;
    ParseStandardURL(new_url.c_str(), new_url.size(), &new_parsed);

    // Canonicalize the combined url.
    return CanonicalizeStandardURL(new_url.c_str(), new_url.size(), new_parsed,
                                   query_converter, output, out_parsed);
  }

  // When we get here, we know that the relative URL is on the same host.
  return DoResolveRelativePath(base_url, base_parsed, base_is_file,
                               relative_url, relative_component,
                               query_converter, output, out_parsed);
}

}  // namespace url

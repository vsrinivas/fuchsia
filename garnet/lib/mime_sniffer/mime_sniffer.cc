// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/mime_sniffer/mime_sniffer.h"

#include <stdint.h>
#include <string>
#include <vector>

#include "lib/fxl/strings/ascii.h"
#include "lib/fxl/strings/string_view.h"

namespace mime_sniffer {

// The number of content bytes we need to use all our magic numbers.
static const size_t kBytesRequiredForMagic = 42;

struct MagicNumber {
  const char* mime_type;
  const char* magic;
  size_t magic_len;
  bool is_string;
  const char* mask;  // if set, must have same length as |magic|
};

// Magic strings are case insensitive and must not include '\0' characters
#define MAGIC_STRING(mime_type, magic) \
  { (mime_type), (magic), sizeof(magic) - 1, true, nullptr }

#define MAGIC_NUMBER(mime_type, magic) \
  { (mime_type), (magic), sizeof(magic) - 1, false, nullptr }

// Our HTML sniffer differs slightly from Mozilla.  For example, Mozilla will
// decide that a document that begins "<!DOCTYPE SOAP-ENV:Envelope PUBLIC " is
// HTML, but we will not.

#define MAGIC_HTML_TAG(tag) MAGIC_STRING("text/html", "<" tag)

static const std::vector<const MagicNumber> kSniffableTags{
    // DOCTYPEs
    MAGIC_HTML_TAG("!DOCTYPE html"),  // HTML5 spec
    // Sniffable tags, ordered by how often they occur in sniffable documents.
    MAGIC_HTML_TAG("script"),  // HTML5 spec, Mozilla
    MAGIC_HTML_TAG("html"),    // HTML5 spec, Mozilla
    MAGIC_HTML_TAG("!--"),
    MAGIC_HTML_TAG("head"),    // HTML5 spec, Mozilla
    MAGIC_HTML_TAG("iframe"),  // Mozilla
    MAGIC_HTML_TAG("h1"),      // Mozilla
    MAGIC_HTML_TAG("div"),     // Mozilla
    MAGIC_HTML_TAG("font"),    // Mozilla
    MAGIC_HTML_TAG("table"),   // Mozilla
    MAGIC_HTML_TAG("a"),       // Mozilla
    MAGIC_HTML_TAG("style"),   // Mozilla
    MAGIC_HTML_TAG("title"),   // Mozilla
    MAGIC_HTML_TAG("b"),       // Mozilla
    MAGIC_HTML_TAG("body"),    // Mozilla
    MAGIC_HTML_TAG("br"),
    MAGIC_HTML_TAG("p"),  // Mozilla
};

// Compare content header to a magic number where magic_entry can contain '.'
// for single character of anything, allowing some bytes to be skipped.
static bool MagicCmp(const char* magic_entry, const char* content, size_t len) {
  while (len) {
    if ((*magic_entry != '.') && (*magic_entry != *content))
      return false;
    ++magic_entry;
    ++content;
    --len;
  }
  return true;
}

// Like MagicCmp() except that it ANDs each byte with a mask before
// the comparison, because there are some bits we don't care about.
static bool MagicMaskCmp(const char* magic_entry, const char* content,
                         size_t len, const char* mask) {
  while (len) {
    if ((*magic_entry != '.') && (*magic_entry != (*mask & *content)))
      return false;
    ++magic_entry;
    ++content;
    ++mask;
    --len;
  }
  return true;
}

static bool MatchMagicNumber(const char* content, size_t size,
                             const MagicNumber& magic_entry,
                             std::string* result) {
  const size_t len = magic_entry.magic_len;

  // Keep kBytesRequiredForMagic honest.
  FXL_DCHECK(len <= kBytesRequiredForMagic);

  // To compare with magic strings, we need to compute strlen(content), but
  // content might not actually have a null terminator.  In that case, we
  // pretend the length is content_size.
  const char* end = static_cast<const char*>(memchr(content, '\0', size));
  const size_t content_strlen =
      (end != nullptr) ? static_cast<size_t>(end - content) : size;

  bool match = false;
  if (magic_entry.is_string) {
    if (content_strlen >= len) {
      // Do a case-insensitive prefix comparison.
      FXL_DCHECK(strlen(magic_entry.magic) == len);
      match = fxl::EqualsCaseInsensitiveASCII(
          fxl::StringView(magic_entry.magic, len),
          fxl::StringView(content, len));
    }
  } else {
    if (size >= len) {
      if (!magic_entry.mask) {
        match = MagicCmp(magic_entry.magic, content, len);
      } else {
        match = MagicMaskCmp(magic_entry.magic, content, len, magic_entry.mask);
      }
    }
  }

  if (match) {
    result->assign(magic_entry.mime_type);
    return true;
  }
  return false;
}

static bool CheckForMagicNumbers(
    const char* content, size_t size,
    const std::vector<const MagicNumber>& magic_numbers, std::string* result) {
  for (const MagicNumber& magic : magic_numbers) {
    if (MatchMagicNumber(content, size, magic, result))
      return true;
  }
  return false;
}

// Truncates |size| to |max_size| and returns true if |size| is at least
// |max_size|.
static bool TruncateSize(const size_t max_size, size_t* size) {
  // Keep kMaxBytesToSniff honest.
  FXL_DCHECK(static_cast<int>(max_size) <= kMaxBytesToSniff);

  if (*size >= max_size) {
    *size = max_size;
    return true;
  }
  return false;
}

// Returns true and sets result if the content appears to be HTML.
// Clears have_enough_content if more data could possibly change the result.
bool SniffForHTML(const char* content, size_t size, bool* have_enough_content,
                  std::string* result) {
  // For HTML, we are willing to consider up to 512 bytes. This may be overly
  // conservative as IE only considers 256.
  *have_enough_content &= TruncateSize(512, &size);

  // We adopt a strategy similar to that used by Mozilla to sniff HTML tags,
  // but with some modifications to better match the HTML5 spec.
  const char* const end = content + size;
  const char* pos;
  for (pos = content; pos < end; ++pos) {
    if (!fxl::IsAsciiWhitespace(*pos))
      break;
  }
  // |pos| now points to first non-whitespace character (or at end).
  return CheckForMagicNumbers(pos, end - pos, kSniffableTags, result);
}

}  // namespace mime_sniffer

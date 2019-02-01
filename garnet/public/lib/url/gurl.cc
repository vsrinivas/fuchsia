// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <pthread.h>

#include <algorithm>
#include <ostream>

#include "lib/url/gurl.h"

#include "lib/fxl/logging.h"
#include "lib/fxl/strings/ascii.h"
#include "lib/fxl/strings/string_view.h"
#include "lib/url/url_canon_stdstring.h"
#include "lib/url/url_util.h"
#include "lib/url/url_util_internal.h"

namespace url {

namespace {

static std::string* empty_string = NULL;
static GURL* empty_gurl = NULL;

static pthread_once_t empty_string_once = PTHREAD_ONCE_INIT;
static pthread_once_t empty_gurl_once = PTHREAD_ONCE_INIT;

void EmptyStringForGURLOnce(void) { empty_string = new std::string; }

const std::string& EmptyStringForGURL() {
  // Avoid static object construction/destruction on startup/shutdown.
  pthread_once(&empty_string_once, EmptyStringForGURLOnce);
  return *empty_string;
}

}  // namespace

GURL::GURL() : is_valid_(false) {}

GURL::GURL(const GURL& other)
    : spec_(other.spec_), is_valid_(other.is_valid_), parsed_(other.parsed_) {}

GURL::GURL(const std::string& url_string) { InitCanonical(url_string, true); }

GURL::GURL(const std::string& url_string, RetainWhiteSpaceSelector) {
  InitCanonical(url_string, false);
}

GURL::GURL(const char* canonical_spec, size_t canonical_spec_len,
           const url::Parsed& parsed, bool is_valid)
    : spec_(canonical_spec, canonical_spec_len),
      is_valid_(is_valid),
      parsed_(parsed) {
  InitializeFromCanonicalSpec();
}

GURL::GURL(std::string canonical_spec, const url::Parsed& parsed, bool is_valid)
    : is_valid_(is_valid), parsed_(parsed) {
  spec_.swap(canonical_spec);
  InitializeFromCanonicalSpec();
}

void GURL::InitCanonical(fxl::StringView input_spec, bool trim_path_end) {
  // Reserve enough room in the output for the input, plus some extra so that
  // we have room if we have to escape a few things without reallocating.
  spec_.reserve(input_spec.size() + 32);
  url::StdStringCanonOutput output(&spec_);
  is_valid_ =
      url::Canonicalize(input_spec.data(), static_cast<int>(input_spec.size()),
                        trim_path_end, NULL, &output, &parsed_);

  output.Complete();  // Must be done before using string.
}

void GURL::InitializeFromCanonicalSpec() {
#ifndef NDEBUG
  // For testing purposes, check that the parsed canonical URL is identical to
  // what we would have produced. Skip checking for invalid URLs have no meaning
  // and we can't always canonicalize then reproducibly.
  if (is_valid_) {
    // We need to retain trailing whitespace on path URLs, as the |parsed_|
    // spec we originally received may legitimately contain trailing white-
    // space on the path or  components e.g. if the #ref has been
    // removed from a "foo:hello #ref" URL (see http://crbug.com/291747).
    GURL test_url(spec_, RETAIN_TRAILING_PATH_WHITEPACE);

    FXL_DCHECK(test_url.is_valid_ == is_valid_);
    FXL_DCHECK(test_url.spec_ == spec_);

    FXL_DCHECK(test_url.parsed_.scheme == parsed_.scheme);
    FXL_DCHECK(test_url.parsed_.username == parsed_.username);
    FXL_DCHECK(test_url.parsed_.password == parsed_.password);
    FXL_DCHECK(test_url.parsed_.host == parsed_.host);
    FXL_DCHECK(test_url.parsed_.port == parsed_.port);
    FXL_DCHECK(test_url.parsed_.path == parsed_.path);
    FXL_DCHECK(test_url.parsed_.query == parsed_.query);
    FXL_DCHECK(test_url.parsed_.ref == parsed_.ref);
  }
#endif
}

GURL::~GURL() {}

GURL& GURL::operator=(GURL other) {
  Swap(&other);
  return *this;
}

const std::string& GURL::spec() const {
  if (is_valid_ || spec_.empty())
    return spec_;

  FXL_DCHECK(false) << "Trying to get the spec of an invalid URL!";
  return EmptyStringForGURL();
}

bool GURL::operator==(const GURL& other) const { return spec_ == other.spec_; }

bool GURL::operator!=(const GURL& other) const { return spec_ != other.spec_; }

bool GURL::operator<(const GURL& other) const { return spec_ < other.spec_; }

bool GURL::operator>(const GURL& other) const { return spec_ > other.spec_; }

// Note: code duplicated below (it's inconvenient to use a template here).
GURL GURL::Resolve(const std::string& relative) const {
  // Not allowed for invalid URLs.
  if (!is_valid_)
    return GURL();

  GURL result;

  // Reserve enough room in the output for the input, plus some extra so that
  // we have room if we have to escape a few things without reallocating.
  result.spec_.reserve(spec_.size() + 32);
  url::StdStringCanonOutput output(&result.spec_);

  if (!url::ResolveRelative(spec_.data(), static_cast<int>(spec_.size()),
                            parsed_, relative.data(),
                            static_cast<int>(relative.size()), nullptr, &output,
                            &result.parsed_)) {
    // Error resolving, return an empty URL.
    return GURL();
  }

  output.Complete();
  result.is_valid_ = true;
  return result;
}

GURL GURL::GetWithEmptyPath() const {
  // This doesn't make sense for invalid or nonstandard URLs, so return
  // the empty URL.
  if (!is_valid_ || !IsStandard())
    return GURL();

  // We could optimize this since we know that the URL is canonical, and we are
  // appending a canonical path, so avoiding re-parsing.
  GURL other(*this);
  if (parsed_.path.is_invalid_or_empty())
    return other;

  // Clear everything after the path.
  other.parsed_.query.reset();
  other.parsed_.ref.reset();

  // Set the path, since the path is longer than one, we can just set the
  // first character and resize.
  other.spec_[other.parsed_.path.begin] = '/';
  other.parsed_.path.set_len(1);
  other.spec_.resize(other.parsed_.path.begin + 1);
  return other;
}

bool GURL::IsStandard() const {
  return url::IsStandard(spec_.data(), parsed_.scheme);
}

bool GURL::SchemeIs(const char* lower_ascii_scheme) const {
  if (parsed_.scheme.is_invalid_or_empty())
    return lower_ascii_scheme == NULL;
  return url::LowerCaseEqualsASCII(
      fxl::StringView(spec_.data() + parsed_.scheme.begin,
                      parsed_.scheme.len()),
      fxl::StringView(lower_ascii_scheme));
}

bool GURL::SchemeIsHTTPOrHTTPS() const {
  return SchemeIs(url::kHttpScheme) || SchemeIs(url::kHttpsScheme);
}

bool GURL::SchemeIsWSOrWSS() const {
  return SchemeIs(url::kWsScheme) || SchemeIs(url::kWssScheme);
}

int GURL::IntPort() const {
  if (parsed_.port.is_nonempty())
    return url::ParsePort(spec_.data(), parsed_.port);
  return url::PORT_UNSPECIFIED;
}

int GURL::EffectiveIntPort() const {
  int int_port = IntPort();
  if (int_port == url::PORT_UNSPECIFIED && IsStandard())
    return url::DefaultPortForScheme(spec_.data() + parsed_.scheme.begin,
                                     parsed_.scheme.len());
  return int_port;
}

std::string GURL::ExtractFileName() const {
  url::Component file_component;
  url::ExtractFileName(spec_.data(), parsed_.path, &file_component);
  return ComponentString(file_component);
}

std::string GURL::PathForRequest() const {
  FXL_DCHECK(parsed_.path.is_nonempty())
      << "Canonical path for requests should be non-empty";
  if (parsed_.ref.is_valid()) {
    // Clip off the reference when it exists. The reference starts after the
    // #-sign, so we have to subtract one to also remove it.
    return std::string(spec_, parsed_.path.begin,
                       parsed_.ref.begin - parsed_.path.begin - 1);
  }
  // Compute the actual path length, rather than depending on the spec's
  // terminator. If we're an inner_url, our spec continues on into our outer
  // URL's path/query/ref.
  size_t path_len = parsed_.path.len();
  if (parsed_.query.is_valid())
    path_len = parsed_.query.end() - parsed_.path.begin;

  return std::string(spec_, parsed_.path.begin, path_len);
}

std::string GURL::HostNoBrackets() const {
  // If host looks like an IPv6 literal, strip the square brackets.
  url::Component h(parsed_.host);
  if (h.is_valid() && h.len() >= 2 && spec_[h.begin] == '[' &&
      spec_[h.end() - 1] == ']') {
    h.begin++;
    h.set_len(h.len() - 2);
  }
  return ComponentString(h);
}

std::string GURL::GetContent() const {
  return is_valid_ ? ComponentString(parsed_.GetContent()) : std::string();
}

bool GURL::HostIsIPAddress() const {
  if (!is_valid_ || spec_.empty())
    return false;

  url::RawCanonOutputT<char, 128> ignored_output;
  url::CanonHostInfo host_info;
  url::CanonicalizeIPAddress(spec_.c_str(), parsed_.host, &ignored_output,
                             &host_info);
  return host_info.IsIPAddress();
}

void EmptyGURLOnce(void) { empty_gurl = new GURL; }

const GURL& GURL::EmptyGURL() {
  // Avoid static object construction/destruction on startup/shutdown.
  pthread_once(&empty_gurl_once, EmptyGURLOnce);
  return *empty_gurl;
}

bool GURL::DomainIs(fxl::StringView lower_ascii_domain) const {
  if (!is_valid_ || lower_ascii_domain.empty())
    return false;

  if (parsed_.host.is_invalid_or_empty())
    return false;

  // If the host name ends with a dot but the input domain doesn't,
  // then we ignore the dot in the host name.
  const char* host_last_pos = spec_.data() + parsed_.host.end() - 1;
  size_t host_len = parsed_.host.len();
  size_t domain_len = lower_ascii_domain.size();
  if ('.' == *host_last_pos && '.' != lower_ascii_domain[domain_len - 1]) {
    host_last_pos--;
    host_len--;
  }

  if (host_len < domain_len)
    return false;

  // |host_first_pos| is the start of the compared part of the host name, not
  // start of the whole host name.
  const char* host_first_pos =
      spec_.data() + parsed_.host.begin + host_len - domain_len;

  if (!url::LowerCaseEqualsASCII(fxl::StringView(host_first_pos, domain_len),
                                 lower_ascii_domain))
    return false;

  // Make sure there aren't extra characters in host before the compared part;
  // if the host name is longer than the input domain name, then the character
  // immediately before the compared part should be a dot. For example,
  // www.google.com has domain "google.com", but www.iamnotgoogle.com does not.
  if ('.' != lower_ascii_domain[0] && host_len > domain_len &&
      '.' != *(host_first_pos - 1))
    return false;

  return true;
}

void GURL::Swap(GURL* other) {
  spec_.swap(other->spec_);
  std::swap(is_valid_, other->is_valid_);
  std::swap(parsed_, other->parsed_);
}

std::ostream& operator<<(std::ostream& out, const GURL& url) {
  return out << url.possibly_invalid_spec();
}

}  // namespace url

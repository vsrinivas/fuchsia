// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "url/scheme_host_port.h"

#include <string.h>

#include "base/logging.h"
#include "base/strings/string_number_conversions.h"
#include "url/gurl.h"
#include "url/url_canon.h"
#include "url/url_canon_stdstring.h"
#include "url/url_constants.h"
#include "url/url_util.h"

namespace url {

SchemeHostPort::SchemeHostPort() : port_(0) {
}

SchemeHostPort::SchemeHostPort(base::StringPiece scheme,
                               base::StringPiece host,
                               uint16 port)
    : scheme_(scheme.data(), scheme.length()),
      host_(host.data(), host.length()),
      port_(port) {
  // Try to canonicalize the host (copy/pasted from net/base. :( ).
  const url::Component raw_host_component(0, static_cast<int>(host.length()));
  std::string canon_host;
  url::StdStringCanonOutput canon_host_output(&canon_host);
  url::CanonHostInfo host_info;
  url::CanonicalizeHostVerbose(host.data(), raw_host_component,
                               &canon_host_output, &host_info);

  if (host_info.out_host.is_nonempty() &&
      host_info.family != url::CanonHostInfo::BROKEN) {
    // Success!  Assert that there's no extra garbage.
    canon_host_output.Complete();
    DCHECK_EQ(host_info.out_host.len, static_cast<int>(canon_host.length()));
  } else {
    // Empty host, or canonicalization failed.
    canon_host.clear();
  }

  // Return an invalid SchemeHostPort object if any of the following conditions
  // hold:
  //
  // 1. The provided scheme is non-standard, 'blob:', or 'filesystem:'.
  // 2. The provided host is non-canonical.
  // 3. The scheme is 'file' and the port is non-zero.
  // 4. The scheme is not 'file', and the port is zero or the host is empty.
  bool isUnsupportedScheme =
      !url::IsStandard(scheme.data(),
                       url::Component(0, static_cast<int>(scheme.length()))) ||
      scheme == kFileSystemScheme || scheme == kBlobScheme;
  bool isNoncanonicalHost = host != canon_host;
  bool isFileSchemeWithPort = scheme == kFileScheme && port != 0;
  bool isNonFileSchemeWithoutPortOrHost =
      scheme != kFileScheme && (port == 0 || host.empty());
  if (isUnsupportedScheme || isNoncanonicalHost || isFileSchemeWithPort ||
      isNonFileSchemeWithoutPortOrHost) {
    scheme_.clear();
    host_.clear();
    port_ = 0;
  }
}

SchemeHostPort::SchemeHostPort(const GURL& url) : port_(0) {
  if (!url.is_valid() || !url.IsStandard())
    return;

  // These schemes do not follow the generic URL syntax, so we treat them as
  // invalid (scheme, host, port) tuples (even though such URLs' _Origin_ might
  // have a (scheme, host, port) tuple, they themselves do not).
  if (url.SchemeIsBlob() || url.SchemeIsFileSystem())
    return;

  scheme_ = url.scheme();
  host_ = url.host();
  port_ = url.EffectiveIntPort() == url::PORT_UNSPECIFIED
              ? 0
              : url.EffectiveIntPort();
}

SchemeHostPort::~SchemeHostPort() {
}

bool SchemeHostPort::IsInvalid() const {
  return scheme_.empty() && host_.empty() && !port_;
}

std::string SchemeHostPort::Serialize() const {
  std::string result;
  if (IsInvalid())
    return result;

  bool is_default_port =
      port_ == url::DefaultPortForScheme(scheme_.data(),
                                         static_cast<int>(scheme_.length()));

  result.append(scheme_);
  result.append(kStandardSchemeSeparator);
  result.append(host_);

  if (scheme_ != kFileScheme && !is_default_port) {
    result.push_back(':');
    result.append(base::IntToString(port_));
  }

  return result;
}

bool SchemeHostPort::Equals(const SchemeHostPort& other) const {
  return port_ == other.port() && scheme_ == other.scheme() &&
         host_ == other.host();
}

bool SchemeHostPort::operator<(const SchemeHostPort& other) const {
  if (port_ != other.port_)
    return port_ < other.port_;
  if (scheme_ != other.scheme_)
    return scheme_ < other.scheme_;
  if (host_ != other.host_)
    return host_ < other.host_;
  return false;
}

}  // namespace url

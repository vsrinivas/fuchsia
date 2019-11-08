// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'tld_checker.dart';

String sanitizeUrl(String url) {
  // Checks if the input starts with a scheme.
  String scheme;
  try {
    scheme = Uri.parse(url).scheme;
  } on FormatException {
    return googleKeyword(url);
  }

  // Checks if the scheme is valid.
  // We currently only supports http, https and chrome.
  // localhost is included in the validSchemePattern since it is recognized
  // as a scheme by the dart Uri.parse method whene there is no other scheme.
  final validSchemePattern = RegExp(r'^(https?|chrome|localhost)$');
  if (scheme.isNotEmpty) {
    if (validSchemePattern.hasMatch(scheme)) {
      return url;
    }
    return googleKeyword(url);
  }

  // Adds a scheme to get a more accurate output from Uri.parse().host
  String schemedUrl = 'https://$url';
  String hostUrl = Uri.parse(schemedUrl).host;

  // Checks if the host url has a valid pattern.
  // Uri.parse().host does not check the validity.
  final validHostPattern = RegExp(r'([a-zA-Z0-9@_-]{1,256}[\.]{1,1})+[\w]+');
  if (validHostPattern.stringMatch(hostUrl) != hostUrl) {
    return googleKeyword(url);
  }

  // Checks if the URL has a valid TLD.
  String tld = hostUrl.split('.').last;
  if (TldChecker().isValid(tld)) {
    return schemedUrl;
  }

  // Checks if the URL is IPv4 address.
  final validIpv4Pattern = RegExp(
      r'\b((25[0-5]|2[0-4][0-9]|1[0-9][0-9]|[1-9]?[0-9])\.){3,3}(25[0-5]|2[0-4][0-9]|1[0-9][0-9]|[1-9]?[0-9])$');
  if (validIpv4Pattern.stringMatch(hostUrl) == hostUrl) {
    return schemedUrl;
  }

  return googleKeyword(url);
}

String googleKeyword(String keyword) =>
    'https://www.google.com/search?q=${Uri.encodeQueryComponent(keyword)}';

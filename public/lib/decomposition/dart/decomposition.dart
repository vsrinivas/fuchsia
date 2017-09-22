// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// Decomposes a URI into base components for evaluation by Resolver and
/// modules.
Map<String, dynamic> decomposeUri(final Uri uri) {
  return {
    'uri': uri.toString(),
    'scheme': uri.scheme,
    'host': uri.host,
    'path': uri.path,
    'path segments': uri.pathSegments,
    'query parameters': uri.queryParameters,
  };
}

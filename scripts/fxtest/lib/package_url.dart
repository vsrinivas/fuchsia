// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:fxtest/fxtest.dart';
import 'package:meta/meta.dart';

/// Deconstructed Fuchsia Package Url used to precisely target URL components.
class PackageUrl {
  /// Root chunk of the Package URL.
  ///
  /// ```
  /// fuchsia.com
  /// ```
  ///
  /// from
  ///
  /// ```
  /// fuchsia-pkg://fuchsia.com/pkg-name/variant?hash=1234#meta/component-name.cmx
  /// ```
  final String host;

  /// First chunk from the URI.
  ///
  /// ```
  /// pkg-name
  /// ```
  ///
  /// from
  ///
  /// ```
  /// fuchsia-pkg://fuchsia.com/pkg-name/variant?hash=1234#meta/component-name.cmx
  /// ```
  final String packageName;

  /// Optional. Second chunk in the URI.
  ///
  /// ```
  /// variant
  /// ```
  ///
  /// from
  ///
  /// ```
  /// fuchsia-pkg://fuchsia.com/pkg-name/variant?hash=1234#meta/component-name.cmx
  /// ```
  final String packageVariant;

  /// Optional. Value for "hash" querystring value.
  ///
  /// ```
  /// 1234
  /// ```
  ///
  /// from
  ///
  /// ```
  /// fuchsia-pkg://fuchsia.com/pkg-name/variant?hash=1234#meta/component-name.cmx
  /// ```
  final String hash;

  /// Component name with the extension.
  ///
  /// ```
  /// component-name.cmx
  /// ```
  ///
  /// from
  ///
  /// ```
  /// fuchsia-pkg://fuchsia.com/pkg-name/variant?hash=1234#meta/component-name.cmx
  /// ```
  final String resourcePath;

  /// Component name without the extension.
  ///
  /// ```
  /// component-name
  /// ```
  ///
  /// from
  ///
  /// ```
  /// fuchsia-pkg://fuchsia.com/pkg-name/variant?hash=1234#meta/component-name.cmx
  /// ```
  final String rawResource;
  PackageUrl({
    @required this.host,
    @required this.packageName,
    @required this.packageVariant,
    @required this.hash,
    @required this.resourcePath,
    @required this.rawResource,
  });

  PackageUrl.none()
      : host = null,
        hash = null,
        packageName = null,
        packageVariant = null,
        resourcePath = null,
        rawResource = null;

  /// Breaks out a canonical Fuchsia URL into its constituent parts.
  ///
  /// Parses something like
  /// `fuchsia-pkg://host/package_name/variant?hash=1234#PATH.cmx` into:
  ///
  /// ```dart
  /// PackageUrl(
  ///   'host': 'host',
  ///   'packageName': 'package_name',
  ///   'packageVariant': 'variant',
  ///   'hash': '1234',
  ///   'resourcePath': 'PATH.cmx',
  ///   'rawResource': 'PATH',
  /// );
  /// ```
  factory PackageUrl.fromString(String packageUrl) {
    Uri parsedUri = Uri.parse(packageUrl);

    if (parsedUri.scheme != 'fuchsia-pkg') {
      throw MalformedFuchsiaUrlException(packageUrl);
    }

    return PackageUrl(
      host: parsedUri.host,
      packageName:
          parsedUri.pathSegments.isNotEmpty ? parsedUri.pathSegments[0] : null,
      packageVariant:
          parsedUri.pathSegments.length > 1 ? parsedUri.pathSegments[1] : null,
      hash: parsedUri.queryParameters['hash'],
      resourcePath: PackageUrl._removeMetaPrefix(parsedUri.fragment),
      rawResource: PackageUrl._removeMetaPrefix(
        PackageUrl._removeExtension(parsedUri.fragment),
      ),
    );
  }

  static String _removeMetaPrefix(String resourcePath) {
    const token = 'meta/';
    return resourcePath.startsWith(token)
        ? resourcePath.substring(token.length)
        : resourcePath;
  }

  static String _removeExtension(String resourcePath) {
    // Guard against uninteresting edge cases
    if (resourcePath == null || !resourcePath.contains('.')) {
      return resourcePath ?? '';
    }
    return resourcePath.substring(0, resourcePath.lastIndexOf('.'));
  }
}

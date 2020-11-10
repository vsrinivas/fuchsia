// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';
import 'dart:collection';
import 'dart:convert';
import 'dart:io';

import 'package:fxtest/fxtest.dart';
import 'package:meta/meta.dart';
import 'package:path/path.dart' as p;

const _defaultManifest = 'package-repositories.json';

/// Contains information about a Fuchsia TUF package repository.
/// Meta information, like file system location, is collected from the file
/// 'package-repositories.json' produced by the Fuchsia build, while the
/// mapping of Fuchsia package URLs to their Merkle root hashes is collected
/// from a 'targets.json' file linked by the 'targets' property of
/// 'package-repositories.json'.
/// The 'targets.json' file is expected to follow the syntax defined in
///  https://fuchsia.dev/fuchsia-src/concepts/system/software_update_system
/// and https://github.com/theupdateframework/specification/blob/HEAD/tuf-spec.md.
class PackageRepository {
  String targetsFile;
  String blobsDirectory;
  String rootPath;

  final Map<String, PackageInfo> _packages = {};

  /// Parses a package-repositories.json manifest file, which points to a
  /// [TUF](https://github.com/theupdateframework/specification) repository.
  /// The repository targets.json file is then parsed and produces a map of
  /// package name and package variant to their corresponding Merkle root
  /// hashes.
  /// Returns null if the 'package-repositories.json' manifest doesn't exist
  /// or if the 'targets.json' file referenced in property 'targets' of
  /// 'package-repositories.json' cannot be opened.
  static Future<PackageRepository> fromManifest(
      {@required String buildDir, String repositoriesFile = _defaultManifest}) {
    // The package-repositories manifest is usually very small, so it's ok to
    // read it all at once.
    File file = File(p.join(buildDir, repositoriesFile));
    if (!file.existsSync()) {
      return null;
    }
    String content = file.readAsStringSync();

    try {
      PackageRepository repository =
          PackageRepository.fromJson(jsonDecode(content));
      File targetsFile = File(p.join(buildDir, repository.targetsFile));
      if (!targetsFile.existsSync()) {
        return null;
      }

      // The targets.json file is usually large, so using a stream instead
      return repository
          .loadTargetsFromJson(targetsFile
              .openRead()
              .transform(utf8.decoder)
              .transform(json.decoder)
              .cast())
          .then((v) => repository);
    } on PackageRepositoryException catch (e) {
      // Wrap the exception to include the manifest filename
      e.file = repositoriesFile;
      rethrow;
    }
  }

  // ignore: prefer_constructors_over_static_methods
  static PackageUrl decoratePackageUrlWithHash(
      PackageRepository repository, String packageUrl) {
    if (packageUrl == null) {
      return null;
    }
    PackageUrl parsed = PackageUrl.fromString(packageUrl);
    if (repository == null || parsed.packageName == null) {
      return parsed;
    }
    PackageInfo info = repository[parsed.packageName];
    if (info == null) {
      return parsed;
    }
    var hash = parsed.packageVariant == null
        ? info.merkle
        : info[parsed.packageVariant];
    return PackageUrl.copyWithHash(other: parsed, hash: hash);
  }

  @visibleForTesting
  PackageRepository(this.targetsFile, this.blobsDirectory, this.rootPath);

  /// Constructs a [PackageRepository] from the contents of a
  /// package-repositories.json manifest file.
  @visibleForTesting
  PackageRepository.fromJson(List<dynamic> manifest) {
    if (manifest.length > 1) {
      throw PackageRepositoryParseException(
          'Multiple repositories are not supported');
    }
    if (manifest.isEmpty) {
      throw PackageRepositoryParseException('No repository found in manifest');
    }
    Map<String, dynamic> repositoryJson = manifest[0];
    targetsFile = repositoryJson['targets'];
    blobsDirectory = repositoryJson['blobs'];
    rootPath = repositoryJson['path'];
  }

  @visibleForTesting
  Future loadTargetsFromJson(Stream<Map<String, dynamic>> jsonStream) {
    return jsonStream
        // Filters objects with signed.targets content
        .where((jsonObj) =>
            jsonObj['signed'] != null && jsonObj['signed']['targets'] != null)
        // Expands each signed.targets map into its entries
        .expand((jsonObj) => jsonObj['signed']['targets'].entries)
        // Merges target entries to _packages
        // ignore: unnecessary_lambdas
        .forEach((targetEntry) => _mergeTarget(targetEntry));
  }

  void _mergeTarget(MapEntry<String, dynamic> targetEntry) {
    var split = targetEntry.key.split('/');
    var name = split[0];
    var variant = split[1];
    _packages[name] =
        PackageInfo.fromJson(name, variant, targetEntry.value, _packages[name]);
  }

  Map<String, PackageInfo> asMap() => UnmodifiableMapView(_packages);

  PackageInfo operator [](String packageName) => _packages[packageName];
}

class PackageInfo {
  final String packageName;

  /// Map package variant to its Merkle root hash
  final Map<String, String> _merkle = {};

  PackageInfo._internal(this.packageName);

  factory PackageInfo.fromJson(
      String name, String variant, Map<String, dynamic> json,
      [PackageInfo current]) {
    var packageInfo = current ?? PackageInfo._internal(name);

    if (packageInfo._merkle.containsKey(variant)) {
      throw PackageRepositoryParseException(
          'Duplicated variant $variant for package $name in'
          'package repository targets file');
    }
    packageInfo._merkle[variant] = json['custom']['merkle'];
    return packageInfo;
  }

  /// Merkle root hash of the package for a given variant.
  String operator [](String variant) {
    return variant == null ? merkle : _merkle[variant];
  }

  /// Merkle root hash of the package.
  /// Throws [PackageRepositoryException] if package has more than one variant.
  String get merkle {
    if (_merkle.length > 1) {
      throw PackageRepositoryException(
          'Package $packageName has more than one variant, please specify one');
    }
    return _merkle.isEmpty ? null : _merkle.values.first;
  }

  @override
  String toString() {
    return 'PackageInfo $packageName: $_merkle';
  }
}

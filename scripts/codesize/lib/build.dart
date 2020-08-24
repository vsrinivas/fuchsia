// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// @dart = 2.8

/// Utilities and type definitions for interacting with a Fuchsia
/// `out/product_config.arch` directory, i.e. the build output directory.
library build;

import 'dart:core';
import 'dart:io';

import 'package:path/path.dart' as path;

/// Represents an output directory for a Fuchsia build.
class Build {
  /// The absolute location of the build directory.
  final Directory dir;

  Build(String buildDir) : dir = Directory(buildDir).absolute;

  /// Creates a `File` object from a path that is relative to the build
  /// directory. Prefer to use this method to access files in the build;
  /// this allows the codesize tool to be run in any working directory.
  /// If path is already absolute, directly open that file.
  File openFile(String pathInBuild) {
    if (path.isAbsolute(pathInBuild)) {
      return File(pathInBuild);
    }
    return dir / File(pathInBuild);
  }

  /// Creates a `Directory` object from a path that is relative to the build
  /// directory. Prefer to use this method to access directories in the build;
  /// this allows the codesize tool to be run in any working directory.
  /// If path is already absolute, directly open that directory.
  Directory openDirectory(String pathInBuild) {
    if (path.isAbsolute(pathInBuild)) {
      return Directory(pathInBuild);
    }
    return dir / Directory(pathInBuild);
  }

  /// Converts any path into one that is relative to the build directory.
  /// Since some Fuchsia prebuilts are located outside the build directory,
  /// this function intentionally allows those cases.
  String rebasePath(String item) {
    if (path.isAbsolute(item)) {
      return path.relative(path.normalize(item), from: dir.path);
    } else {
      return path.normalize(item);
    }
  }

  /// Returns the sibling zircon build output directory.
  Directory zirconBuildDirectory() {
    final buildDirName = path.split(dir.path).last;
    return dir.parent / Directory('$buildDirName.zircon');
  }

  /// Returns the blob manifest listing.
  File blobManifestFile() =>
      openDirectory('obj/build/images') / File('blob.manifest');
}

extension SubDir on Directory {
  /// Constructs a new `FileSystemEntity` located within this directory,
  /// identified by the path component `sub`.
  FileSystemEntity operator /(FileSystemEntity sub) {
    String child = path.join(this.path, path.normalize(sub.path));
    final type = FileSystemEntity.typeSync(child);
    switch (type) {
      case FileSystemEntityType.directory:
        return Directory(child);
      case FileSystemEntityType.file:
      case FileSystemEntityType.notFound:
        if (sub is Directory) return Directory(child);
        if (sub is File) return File(child);
        throw Exception('Unsupported $sub type');
      default:
        throw Exception('Unexpected type $type for $child');
    }
  }
}

/// An ELF object produced by the build.
/// It can either be a standalone blob, or a ELF contained within a
/// Zircon Boot Image (ZBI) blob, see `SubBlob`.
abstract class BuildArtifact implements Comparable {
  /// Path to the file within the build directory.
  String buildPath;

  /// Size of the blob on the compiling machine (host).
  /// This is the uncompressed size.
  int sizeOnHost;

  /// Favor standalone files over ones packaged in a zbi.
  /// Favor in-tree over prebuilt.
  @override
  int compareTo(dynamic other) {
    if (other is BuildArtifact) {
      if (other is SubBlob && !(this is SubBlob)) return 1;
      if (this is SubBlob && !(other is SubBlob)) return -1;

      final compLen = buildPath.length.compareTo(other.buildPath.length);
      if (compLen != 0) return compLen;
      return buildPath.compareTo(other.buildPath);
    } else {
      throw Exception('Cannot compare $this with $other');
    }
  }
}

class Blob extends BuildArtifact {
  /// The merkle root of the blob.
  String hash;

  /// A human-readable name for the blob.
  /// Typically, it is the "source_path" field in the referencing
  /// pakcage manifest. If multiple packages reference a blob
  /// via different paths, there will be multiple elements
  /// in the set.
  Set<String> sourcePaths = <String>{};

  /// Size the blob occupies in the image.
  /// This might be compressed.
  int size;

  /// Number of references to this blob.
  int count;

  /// If the blob is a zbi, we would extract and count the contents
  /// in the zbi too. Those would be listed as `subBlobs`.
  List<SubBlob> subBlobs = [];

  @override
  String toString() => 'Blob { buildPath: $buildPath, sizeOnHost: $sizeOnHost, '
      'hash: $hash, sourcePaths: $sourcePaths, '
      'size: $size, count: $count, subBlobs: $subBlobs }';
}

class SubBlob extends BuildArtifact {
  /// Name of the file relative to the root of the zbi.
  String name;

  @override
  String toString() =>
      'SubBlob { buildPath: $buildPath, sizeOnHost: $sizeOnHost, '
      'name: $name }';
}

class Package {
  String path;
  String name;
  int size;
  int private;
  int blobCount;
  Map<String, Blob> blobsByPath;
}

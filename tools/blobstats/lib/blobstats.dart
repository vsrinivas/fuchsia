// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';
import 'dart:convert';
import 'dart:io';

import 'package:blobstats/blob.dart';
import 'package:blobstats/dart_package.dart';
import 'package:blobstats/package.dart';
import 'package:path/path.dart' as p;

class BlobStats {
  Directory buildDir;
  Directory outputDir;
  String suffix;
  bool humanReadable;
  Map<String, Blob> blobsByHash = <String, Blob>{};
  int duplicatedSize = 0;
  int deduplicatedSize = 0;
  List<File> pendingPackages = <File>[];
  List<Package> packages = <Package>[];

  BlobStats(this.buildDir, this.outputDir, this.suffix,
      {this.humanReadable = false});

  Future addManifest(String dir, String name) async {
    var lines = await File(p.join(buildDir.path, dir, name)).readAsLines();
    for (var line in lines) {
      var parts = line.split('=');
      var hash = parts[0];
      // Path entries are specified relative to the directory containing the manifest.
      var entryPath = p.join(dir, parts[1]);
      var file = File(entryPath);
      if (entryPath.endsWith('meta.far')) {
        pendingPackages.add(file);
      }

      if (suffix != null && !entryPath.endsWith(suffix)) {
        continue;
      }

      var stat = file.statSync();
      if (stat.type == FileSystemEntityType.notFound) {
        print('$entryPath does not exist');
        continue;
      }
      var blob = blobsByHash[hash];
      if (blob == null) {
        var blob = Blob()
          ..hash = hash
          ..buildPath = entryPath
          ..sizeOnHost = stat.size
          ..count = 0;
        blobsByHash[hash] = blob;
      }
    }
  }

  Future addBlobSizes(String path) async {
    var blobs =
        json.decode(await File(p.join(buildDir.path, path)).readAsString());
    for (var blob in blobs) {
      var b = blobsByHash[blob['merkle']];
      if (b != null) {
        b.size = blob['size'];
      }
    }
  }

  void printBlobList(List<Blob> blobs, int limit) {
    print('     Size Share      Prop     Saved Path');
    var n = 0;
    for (var blob in blobs) {
      if (n++ > limit) return;

      var sb = StringBuffer()
        ..write(formatSize(blob.size).padLeft(9))
        ..write(' ')
        ..write(blob.count.toString().padLeft(5))
        ..write(' ')
        ..write(formatSize(blob.proportional).padLeft(9))
        ..write(' ')
        ..write(formatSize(blob.saved).padLeft(9))
        ..write(' ')
        ..write(blob.sourcePath);
      print(sb);
    }
  }

  void printBlobs(int limit) {
    var blobs = blobsByHash.values.toList();
    print('Top blobs by size ($limit of ${blobs.length})');
    blobs.sort((a, b) => b.size.compareTo(a.size));
    printBlobList(blobs, limit);

    blobs.removeWhere((blob) => blob.count == 1);

    print('');
    print('Top deduplicated blobs by proportional ($limit of ${blobs.length})');
    blobs.sort((a, b) => b.proportional.compareTo(a.proportional));
    printBlobList(blobs, limit);

    print('');
    print('Top deduplicated blobs by saved ($limit of ${blobs.length})');
    blobs.sort((a, b) => b.saved.compareTo(a.saved));
    printBlobList(blobs, limit);
  }

  void printOverallSavings() {
    var percent = (duplicatedSize - deduplicatedSize) * 100 ~/ duplicatedSize;
    print('');
    print('Total savings from deduplication:');
    print(
        '   $percent% ${formatSize(deduplicatedSize)} / ${formatSize(duplicatedSize)}');
  }

  String metaFarToBlobsJson(String farPath) {
    // Assumes details of //build/package.gni, namely that it generates
    //   <build-dir>/.../<package>/meta.far
    // and puts a blobs.json file into
    //   <build-dir>/.../<package>/blobs.json
    if (!farPath.endsWith('/meta.far')) {
      throw ArgumentError('Build details have changed');
    }
    String path = '${removeSuffix(farPath, 'meta.far')}blobs.json';
    if (!File(path).existsSync()) {
      throw ArgumentError(
          'Build details have changed - path to blobs.json $path not found for $farPath');
    }
    return path;
  }

  Future computePackagesInParallel(int jobs) async {
    var tasks = <Future>[];
    for (var i = 0; i < jobs; i++) {
      tasks.add(computePackages());
    }
    await Future.wait(tasks);
  }

  Future computePackages() async {
    while (pendingPackages.isNotEmpty) {
      File far = pendingPackages.removeLast();

      var package = Package()..path = far.path;
      var parts = package.path.split('/');
      package
        ..name = removeSuffix(
            parts.length > 1 ? parts[parts.length - 2] : parts.last, '.meta')
        ..size = 0
        ..proportional = 0
        ..private = 0
        ..blobCount = 0
        ..blobsByPath = <String, Blob>{};

      var blobs =
          json.decode(await File(metaFarToBlobsJson(far.path)).readAsString());

      for (var blob in blobs) {
        var hash = blob['merkle'];
        var path = blob['path'];
        var b = blobsByHash[hash];
        if (b == null) {
          print(
              '$path $hash is in a package manifest but not the final manifest');
          continue;
        }
        b.count++;
        var sourcePath = blob['source_path'];
        // If the source_path looks like <something>/blobs/<merkle>, it from a prebuilt package and has no
        // meaningful source. Instead, use the path within the package as its identifier.
        if (sourcePath.endsWith('/blobs/$hash')) {
          sourcePath = path;
        }
        // We may see the same blob referenced from different packages with different source paths.
        // If all references agree with each other, use that.
        // Otherwise record the first observed path and append ' *' to denote that the path is only one of many.
        if (b.sourcePath == 'Unknown') {
          b.sourcePath = sourcePath;
        } else if (b.sourcePath != sourcePath && !b.sourcePath.endsWith(' *')) {
          b.sourcePath = '${b.sourcePath} *';
        }
        package.blobsByPath[path] = b;
      }

      packages.add(package);
    }
  }

  void computeStats() {
    var filteredBlobs = <String, Blob>{};
    blobsByHash.forEach((hash, blob) {
      if (blob.count == 0) {
        print(
            '${blob.hash} is in the final manifest but not any package manifest');
      } else {
        filteredBlobs[hash] = blob;
      }
    });
    blobsByHash = filteredBlobs;

    for (var blob in blobsByHash.values) {
      duplicatedSize += (blob.size * blob.count);
      deduplicatedSize += blob.size;
    }

    for (var package in packages) {
      for (var blob in package.blobsByPath.values) {
        package
          ..size += blob.size
          ..proportional += blob.proportional;
        if (blob.count == 1) {
          package.private += blob.size;
        }
        package.blobCount++;
      }
    }
  }

  void printPackages() {
    packages.sort((a, b) => b.proportional.compareTo(a.proportional));
    print('');
    print('Packages by proportional (${packages.length})');
    print('     Size      Prop   Private Name');
    for (var package in packages) {
      var sb = StringBuffer()
        ..write(formatSize(package.size).padLeft(9))
        ..write(' ')
        ..write(formatSize(package.proportional).padLeft(9))
        ..write(' ')
        ..write(formatSize(package.private).padLeft(9))
        ..write(' ')
        ..write(package.name);
      print(sb);
    }
  }

  Future packagesToChromiumBinarySizeTree() async {
    var rootTree = {};
    rootTree['n'] = 'packages';
    rootTree['children'] = [];
    rootTree['k'] = 'p'; // kind=path
    for (var pkg in packages) {
      var pkgTree = {};
      pkgTree['n'] = pkg.name;
      pkgTree['children'] = [];
      pkgTree['k'] = 'p'; // kind=path
      rootTree['children'].add(pkgTree);
      pkg.blobsByPath.forEach((path, blob) {
        var blobName = path; //path.split('/').last;
        var blobTree = {};
        blobTree['n'] = blobName;
        blobTree['k'] = 's'; // kind=blob
        var isUnique = blob.count == 1;
        var isDart =
            blobName.endsWith('.dilp') || blobName.endsWith('.aotsnapshot');
        if (isDart) {
          if (isUnique) {
            blobTree['t'] = 'uniDart';
          } else {
            blobTree['t'] = 'dart'; // type=Shared Dart ('blue')
          }
        } else {
          if (isUnique) {
            blobTree['t'] = 'unique';
          } else {
            blobTree['t'] = '?'; // type=Other ('red')
          }
        }
        blobTree['c'] = blob.count;
        blobTree['value'] = blob.proportional;
        blobTree['originalSize'] = blob.sizeOnHost;
        pkgTree['children'].add(blobTree);
      });
    }

    var sink = File(p.join(outputDir.path, 'data.js')).openWrite()
      ..write('var tree_data=')
      ..write(json.encode(rootTree));
    await sink.close();

    await Directory(p.join(outputDir.path, 'd3_v3')).create(recursive: true);
    var d3Dir = p.join(buildDir.path, '../../scripts/third_party/d3_v3/');
    for (var file in ['LICENSE', 'd3.js']) {
      await File(d3Dir + file).copy(p.join(outputDir.path, 'd3_v3', file));
    }
    var templateDir = p.join(buildDir.path, '../../tools/blobstats/template/');
    for (var file in ['index.html', 'D3BlobTreeMap.js']) {
      await File(templateDir + file).copy(p.join(outputDir.path, file));
    }

    print('Wrote visualization to ${p.join(outputDir.path, 'index.html')}');
  }

  void printDartPackages() async {
    var dartPackagesMap = <String, DartPackage>{};
    for (var fuchsiaPackage in packages) {
      fuchsiaPackage.blobsByPath.forEach((path, blob) {
        if (!path.endsWith('.dilp')) return;

        var dartPackageName = removeSuffix(path.split('/').last, '.dilp');
        if (dartPackageName == 'main') return;

        var dartPackage = dartPackagesMap.putIfAbsent(
            dartPackageName, () => DartPackage(dartPackageName));

        dartPackage.blobs.putIfAbsent(blob, () => []).add(fuchsiaPackage.name);
      });
    }

    var dartPackagesList = dartPackagesMap.values.toList()
      ..sort((a, b) => a.name.compareTo(b.name))
      ..sort((a, b) => a.blobs.length.compareTo(b.blobs.length));

    print('');
    print('Dart packages:');
    for (var dartPackage in dartPackagesList) {
      print('package:${dartPackage.name} (${dartPackage.blobs.length} blobs)');
      if (dartPackage.blobs.length == 1) {
        continue;
      }

      for (var blob in dartPackage.blobs.keys) {
        var fuchsiaPackages = dartPackage.blobs[blob];

        var result = await Process.run(Platform.executable, [
          '../../prebuilt/third_party/flutter/x64/debug/jit/dart_binaries/list_libraries.snapshot',
          blob.buildPath
        ]);
        if (result.exitCode != 0) {
          print(result.stdout);
          print(result.stderr);
          throw Exception('Failed to list libraries in kernel file');
        }
        var libraries = result.stdout.split('\n');
        libraries.remove('');

        print('  ${blob.hash} ${blob.buildPath}');
        print('    ${fuchsiaPackages.join(' ')}');
        for (var library in libraries) {
          print('    $library');
        }
      }
      print('');
    }
  }

  Future<String> csvBlobs() async {
    var path = p.join(outputDir.path, 'blobs.csv');
    var csv = File(path).openWrite()..writeln('Size,Share,Prop,Saved,Path');

    var blobs = blobsByHash.values.toList()
      ..sort((a, b) => b.size.compareTo(a.size));

    for (var blob in blobs) {
      var values = [
        blob.size,
        blob.count,
        blob.proportional,
        blob.saved,
        blob.sourcePath
      ];
      csv.writeln(values.join(','));
    }
    await csv.close();
    return path;
  }

  Future<String> csvPackages() async {
    var path = p.join(outputDir.path, 'packages.csv');
    var csv = File(path).openWrite()..writeln('Size,Prop,Private,Name');

    packages.sort((a, b) => b.proportional.compareTo(a.proportional));

    for (var package in packages) {
      var values = [
        package.size,
        package.proportional,
        package.private,
        package.name
      ];
      csv.writeln(values.join(','));
    }
    await csv.close();
    return path;
  }

  String formatSize(num size) {
    if (!humanReadable) return '$size';

    var formattedSize = size;
    if (formattedSize < 1024) return '$formattedSize';
    formattedSize /= 1024;
    if (formattedSize < 1024) return '${formattedSize.toStringAsFixed(1)}K';
    formattedSize /= 1024.0;
    if (formattedSize < 1024) return '${formattedSize.toStringAsFixed(1)}M';
    formattedSize /= 1024;
    return '${formattedSize.toStringAsFixed(1)}G';
  }

  String removeSuffix(String s, String suffix) {
    if (s.endsWith(suffix)) {
      return s.substring(0, s.length - suffix.length);
    }
    return s;
  }
}

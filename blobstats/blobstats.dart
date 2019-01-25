// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import "dart:async";
import "dart:convert";
import "dart:io";

import 'package:args/args.dart';

ArgResults argResults;
const lz4Compression = "lz4-compression";
const zstdCompression = "zstd-compression";
const output = "output";

class Blob {
  String hash;
  String sourcePath = "Unknown";
  int size;
  int sizeOnHost;
  int estimatedCompressedSize;
  int count;

  int get saved {
    return size * (count - 1);
  }

  int get proportional {
    return size ~/ count;
  }
}

class Package {
  String name;
  int size;
  int proportional;
  int private;
  int blobCount;
  Map<String, Blob> blobsByPath;
}

String pathJoin(String part1, String part2, [String part3]) {
  var buffer = new StringBuffer();
  buffer.write(part1);
  if (!part1.endsWith("/")) buffer.write("/");
  buffer.write(part2);
  if (part3 != null) {
    if (!part2.endsWith("/")) buffer.write("/");
    buffer.write(part3);
  }
  return buffer.toString();
}

class BlobStats {
  Directory buildDir;
  Directory outputDir;
  String suffix;
  Map<String, Blob> blobsByHash = new Map<String, Blob>();
  int duplicatedSize = 0;
  int deduplicatedSize = 0;
  List<File> pendingPackages = new List<File>();
  List<Package> packages = new List<Package>();

  BlobStats(
      Directory this.buildDir, Directory this.outputDir, String this.suffix);

  Future addManifest(String path) async {
    var lines = await new File(pathJoin(buildDir.path, path)).readAsLines();
    for (var line in lines) {
      var parts = line.split("=");
      var hash = parts[0];
      var path = parts[1];
      if (!path.startsWith("/")) {
        path = pathJoin(buildDir.path, path);
      }
      var file = new File(path);
      if (path.endsWith("meta.far")) {
        pendingPackages.add(file);
      }

      if (suffix != null && !path.endsWith(suffix)) {
        continue;
      }

      var stat = await file.stat();
      if (stat.type == FileSystemEntityType.NOT_FOUND) {
        print("$path does not exist");
        continue;
      }
      var blob = blobsByHash[hash];
      if (blob == null) {
        var blob = new Blob();
        blob.hash = hash;
        blob.sizeOnHost = stat.size;
        blob.estimatedCompressedSize =
            await estimateCompressedBlobSize(stat.size, hash, path);
        blob.count = 0;
        blobsByHash[hash] = blob;
      }
    }
  }

  Future estimateCompressedBlobSize(int size, String hash, String path) async {
    // TODO(smklein): This is a heuristic matching the internals of blobs.
    // As this heuristic changes (or the compression algorithm is altered),
    // this code must be updated.
    const int minimumSaving = 65536;

    bool compression = argResults[lz4Compression] | argResults[zstdCompression];
    if (size > minimumSaving && compression) {
      String tmpPath = Directory.systemTemp.path + "/compressed." + hash;
      var compressedFile = new File(tmpPath);
      try {
        if (argResults[lz4Compression]) {
          var result = await Process.run("lz4", ["-1", path, tmpPath]);
          if (result.exitCode > 0) {
            print("Could not compress $path");
            return size;
          }
        } else if (argResults[zstdCompression]) {
          var result = await Process.run("zstd", [path, "-o", tmpPath]);
          if (result.exitCode > 0) {
            print("Could not compress $path");
            return size;
          }
        } else {
          print("Bad compression algorithm");
        }
        var stat = await compressedFile.stat();
        if (stat.type == FileSystemEntityType.NOT_FOUND) {
          print("Could not compress $path");
          return size;
        }
        if (stat.size < size - minimumSaving) {
          return stat.size;
        }
      } finally {
        await compressedFile.delete();
      }
    }
    return size; // No compression
  }

  Future addBlobSizes(String path) async {
    var lines = await new File(pathJoin(buildDir.path, path)).readAsLines();
    for (var line in lines) {
      var parts = line.split("=");
      var hash = parts[0];
      var blob = blobsByHash[hash];
      blob.size = int.parse(parts[1]);
    }
  }

  void printBlobList(List<Blob> blobs, int limit) {
    print("     Size Share      Prop     Saved Hash");
    var n = 0;
    for (var blob in blobs) {
      if (n++ > limit) return;

      var sb = new StringBuffer();
      sb.write(blob.size.toString().padLeft(9));
      sb.write(" ");
      sb.write(blob.count.toString().padLeft(5));
      sb.write(" ");
      sb.write(blob.proportional.toString().padLeft(9));
      sb.write(" ");
      sb.write(blob.saved.toString().padLeft(9));
      sb.write(" ");
      sb.write(blob.sourcePath);
      print(sb);
    }
  }

  void printBlobs(int limit) {
    var blobs = blobsByHash.values.toList();
    print("Top blobs by size ($limit of ${blobs.length})");
    blobs.sort((a, b) => b.size.compareTo(a.size));
    printBlobList(blobs, limit);

    blobs.removeWhere((blob) => blob.count == 1);

    print("");
    print("Top deduplicated blobs by proportional ($limit of ${blobs.length})");
    blobs.sort((a, b) => b.proportional.compareTo(a.proportional));
    printBlobList(blobs, limit);

    print("");
    print("Top deduplicated blobs by saved ($limit of ${blobs.length})");
    blobs.sort((a, b) => b.saved.compareTo(a.saved));
    printBlobList(blobs, limit);

    var percent = (duplicatedSize - deduplicatedSize) * 100 ~/ duplicatedSize;

    print("");
    print("Total savings from deduplication:");
    print("   $percent% $deduplicatedSize / $duplicatedSize");
  }

  String metaFarToBlobsJson(String farPath) {
    // Assumes details of //build/package.gni, namely that it generates
    //   <build-dir>/.../<package>.meta/meta.far
    // and puts a blobs.json file into
    //   <build-dir>/.../<package>.meta/blobs.json
    if (!farPath.endsWith(".meta/meta.far")) {
      throw "Build details have changed";
    }
    return farPath.substring(0, farPath.length - "meta.far".length) +
        "blobs.json";
  }

  Future computePackagesInParallel(int jobs) async {
    var tasks = new List<Future>();
    for (var i = 0; i < jobs; i++) {
      tasks.add(computePackages());
    }
    await Future.wait(tasks);
  }

  Future computePackages() async {
    while (!pendingPackages.isEmpty) {
      File far = pendingPackages.removeLast();

      var package = new Package();
      package.name = far.path.substring(buildDir.path.length);
      package.size = 0;
      package.proportional = 0;
      package.private = 0;
      package.blobCount = 0;
      package.blobsByPath = new Map<String, Blob>();

      var blobs = json
          .decode(await new File(metaFarToBlobsJson(far.path)).readAsString());

      for (var blob in blobs) {
        var hash = blob["merkle"];
        var path = blob["path"];
        var b = blobsByHash[hash];
        if (b == null) {
          print(
              "$path $hash is in a package manifest but not the final manifest");
          continue;
        }
        b.count++;
        var sourcePath = blob["source_path"];
        // If the source_path looks like <something>/blobs/<merkle>, it from a prebuilt package and has no
        // meaningful source. Instead, use the path within the package as its identifier.
        if (sourcePath.endsWith("/blobs/$hash")) {
          sourcePath = path;
        }
        // We may see the same blob referenced from different packages with different source paths.
        // If all references agree with each other, use that.
        // Otherwise record the first observed path and append " *" to denote that the path is only one of many.
        if (b.sourcePath == "Unknown") {
          b.sourcePath = sourcePath;
        } else if (b.sourcePath != sourcePath && !b.sourcePath.endsWith(" *")) {
          b.sourcePath = b.sourcePath + " *";
        }
        package.blobsByPath[path] = b;
      }

      packages.add(package);
    }
  }

  void computeStats() {
    var filteredBlobs = new Map<String, Blob>();
    blobsByHash.forEach((hash, blob) {
      if (blob.count == 0) {
        print(
            "${blob.hash} is in the final manifest but not any package manifest");
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
        package.size += blob.size;
        package.proportional += blob.proportional;
        if (blob.count == 1) {
          package.private += blob.size;
        }
        package.blobCount++;
      }
    }
  }

  void printPackages() {
    packages.sort((a, b) => b.proportional.compareTo(a.proportional));
    print("");
    print("Packages by proportional (${packages.length})");
    print("     Size      Prop   Private Path");
    for (var package in packages) {
      var sb = new StringBuffer();
      sb.write(package.size.toString().padLeft(9));
      sb.write(" ");
      sb.write(package.proportional.toString().padLeft(9));
      sb.write(" ");
      sb.write(package.private.toString().padLeft(9));
      sb.write(" ");
      sb.write(package.name);
      print(sb);
    }
  }

  Future packagesToChromiumBinarySizeTree() async {
    var rootTree = {};
    rootTree["n"] = "packages";
    rootTree["children"] = new List();
    rootTree["k"] = "p"; // kind=path
    for (var pkg in packages) {
      var parts = pkg.name.split("/");
      var pkgName = parts.length > 1 ? parts[parts.length - 2] : parts.last;
      var pkgTree = {};
      pkgTree["n"] = pkgName;
      pkgTree["children"] = new List();
      pkgTree["k"] = "p"; // kind=path
      rootTree["children"].add(pkgTree);
      pkg.blobsByPath.forEach((path, blob) {
        var blobName = path; //path.split("/").last;
        var blobTree = {};
        blobTree["n"] = blobName;
        blobTree["k"] = "s"; // kind=blob
        var isUnique = blob.count == 1;
        var isDart =
            blobName.endsWith(".dilp") || blobName.endsWith(".aotsnapshot");
        if (isDart) {
          if (isUnique) {
            blobTree["t"] = "uniDart";
          } else {
            blobTree["t"] = "dart"; // type=Shared Dart ("blue")
          }
        } else {
          if (isUnique) {
            blobTree["t"] = "unique";
          } else {
            blobTree["t"] = "?"; // type=Other ("red")
          }
        }
        blobTree["c"] = blob.count;
        blobTree["value"] = blob.proportional;
        blobTree["originalSize"] = blob.sizeOnHost;
        blobTree["estimatedCompressedSize"] = blob.estimatedCompressedSize;
        pkgTree["children"].add(blobTree);
      });
    }

    await outputDir.create(recursive: true);

    var sink = new File(pathJoin(outputDir.path, "data.js")).openWrite();
    sink.write("var tree_data=");
    sink.write(json.encode(rootTree));
    await sink.close();

    await new Directory(pathJoin(outputDir.path, "d3")).create(recursive: true);
    var d3Dir = pathJoin(buildDir.path, "../../scripts/third_party/d3/");
    for (var file in ["LICENSE", "d3.js"]) {
      await new File(d3Dir + file).copy(pathJoin(outputDir.path, "d3", file));
    }
    var templateDir =
        pathJoin(buildDir.path, "../../scripts/blobstats/template/");
    for (var file in ["index.html", "D3BlobTreeMap.js"]) {
      await new File(templateDir + file).copy(pathJoin(outputDir.path, file));
    }

    print("");
    print("  Wrote visualization to file://" +
        pathJoin(outputDir.path, "index.html"));
  }
}

Future main(List<String> args) async {
  final parser = new ArgParser()
    ..addFlag("help", abbr: "h", help: "give this help")
    ..addOption(output, abbr: "o", help: "Directory to output report to")
    ..addFlag(lz4Compression,
        abbr: "l", defaultsTo: false, help: "Use (lz4) compressed size")
    ..addFlag(zstdCompression,
        abbr: "z", defaultsTo: false, help: "Use (zstd) compressed size");

  argResults = parser.parse(args);
  if (argResults["help"]) {
    print("Usage: fx blobstats [OPTION]...\n\nOptions:\n" + parser.usage);
    return;
  }

  var suffix;
  if (argResults.rest.length > 0) {
    suffix = argResults.rest[0];
  }

  var outputDir = Directory.current;
  if (argResults[output] != null) {
    outputDir = new Directory(argResults[output]);
  }

  var stats = new BlobStats(Directory.current, outputDir, suffix);
  await stats.addManifest("blob.manifest");
  await stats.addBlobSizes("blob.sizes");
  await stats.computePackagesInParallel(Platform.numberOfProcessors);
  stats.computeStats();
  stats.printBlobs(40);
  stats.printPackages();
  await stats.packagesToChromiumBinarySizeTree();
}

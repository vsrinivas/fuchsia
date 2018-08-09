// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import "dart:async";
import "dart:convert";
import "dart:io";

class Blob {
  String hash;
  String path;
  int size;
  int count;

  int get saved { return size * (count - 1); }
  int get proportional { return size ~/ count; }
}

class Package {
  String name;
  int size;
  int proportional;
  int private;
  int blobCount;
  Map<String, Blob> blobsByPath;
}

String pathJoin(String part1,
                String part2, [
                String part3]) {
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
  String suffix;
  List<File> pendingFiles = new List<File>();
  Map<String, Blob> blobsByHash = new Map<String, Blob>();
  int duplicatedSize = 0;
  int deduplicatedSize = 0;
  List<File> pendingPackages = new List<File>();
  List<Package> packages = new List<Package>();

  BlobStats(Directory this.buildDir, String this.suffix);

  Future addManifest(String path) async {
    var lines = await new File(pathJoin(buildDir.path, path)).readAsLines();
    for (var line in lines) {
      var path = line.split("=").last;
      pendingFiles.add(new File(pathJoin(buildDir.path, path)));
    }
  }

  Future<String> computeHash(String path) async {
    return (await Process.run("shasum", [path])).stdout.substring(0, 40);
  }

  Future computeBlobs() async {
    while (!pendingFiles.isEmpty) {
      var file = pendingFiles.removeLast();
      var path = file.path;

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

      var size = stat.size;
      var hash = await computeHash(path);

      var blob = blobsByHash[hash];
      if (blob == null) {
        var blob = new Blob();
        blob.hash = hash;
        blob.path = path;
        blob.size = size;
        blob.count = 0;
        blobsByHash[hash] = blob;
      }
    }
  }

  Future computeBlobsInParallel(int jobs) async {
    var tasks = new List<Future>();
    for (var i = 0; i < jobs; i++) {
      tasks.add(computeBlobs());
    }
    await Future.wait(tasks);
  }

  void printBlobList(List<Blob> blobs, int limit) {
    print("     Size Share      Prop     Saved Path");
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
      sb.write(blob.path.substring(buildDir.path.length));
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

  String farToManifest(String farPath) {
    // Assumes details of //build/package.gni, namely that it generates
    //   <build-dir>/.../<package>.manifest
    //   <build-dir>/.../<package>.meta/meta.far
    // and puts meta.far into
    //   <build-dir>/blobs.manifest
    if (!farPath.endsWith(".meta/meta.far")) {
      throw "Build details have changed";
    }
    return farPath.substring(0, farPath.length - ".meta/meta.far".length) + ".manifest";
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

      for (var line in await new File(farToManifest(far.path)).readAsLines()) {
        var path = line.split("=").last;

        if (suffix != null && !path.endsWith(suffix)) {
          continue;
        }

        var hash = await computeHash(path);
        var blob = blobsByHash[hash];
        if (blob == null) {
          print("$path is in a package manifest but not the final manifest");
          continue;
        }

        blob.count++;
        package.blobsByPath[path] = blob;
      }
      packages.add(package);
    }
  }

  void computeStats() {
    var filteredBlobs = new Map<String, Blob>();
    blobsByHash.forEach((hash, blob) {
      if (blob.count == 0) {
        print("${blob.path} is in the final manifest but not any package manifest");
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
    rootTree["k"] = "p";  // kind=path
    for (var pkg in packages) {
      var parts = pkg.name.split("/");
      var pkgName = parts.length > 1 ? parts[parts.length - 2] : parts.last;
      var pkgTree = {};
      pkgTree["n"] = pkgName;
      pkgTree["children"] = new List();
      pkgTree["k"] = "p";  // kind=path
      rootTree["children"].add(pkgTree);
      pkg.blobsByPath.forEach((path, blob) {
        var blobName = path.split("/").last;
        var blobTree = {};
        blobTree["n"] = blobName;
        blobTree["k"] = "s";  // kind=symbol
        if (blobName.endsWith(".dilp") || blobName.endsWith(".aotsnapshot")) {
          blobTree["t"] = "t";  // type=local text ("blue")
        } else {
          blobTree["t"] = "T";  // type=global text ("red")
        }
        blobTree["value"] = blob.proportional;
        pkgTree["children"].add(blobTree);
      });
    }

    var sink = new File(pathJoin(buildDir.path, "data.js")).openWrite();
    sink.write("var tree_data=");
    sink.write(json.encode(rootTree));
    await sink.close();

    await new Directory(pathJoin(buildDir.path, "d3")).create(recursive: true);
    var d3Dir = buildDir.path + "/../../third_party/dart/runtime/third_party/d3/src/";
    for (var file in ["LICENSE", "d3.js"]) {
      await new File(d3Dir + file).copy(pathJoin(buildDir.path, "d3", file));
    }
    var templateDir = pathJoin(buildDir.path, "../../third_party/dart/runtime/third_party/binary_size/src/template/");
    for (var file in ["index.html", "D3SymbolTreeMap.js"]) {
      await new File(templateDir + file).copy(pathJoin(buildDir.path, file));
    }

    print("");
    print("  Wrote visualization to file://" + pathJoin(buildDir.path, "index.html"));
  }
}

Future main(List<String> args) async {
  var suffix;
  if (args.length > 0) {
    suffix = args[0];
  }

  var stats = new BlobStats(Directory.current, suffix);
  await stats.addManifest("blob.manifest");
  await stats.computeBlobsInParallel(Platform.numberOfProcessors);
  await stats.computePackagesInParallel(Platform.numberOfProcessors);
  stats.computeStats();
  stats.printBlobs(40);
  stats.printPackages();
  await stats.packagesToChromiumBinarySizeTree();
}

// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';
import 'dart:collection';
import 'dart:io';

import 'package:path/path.dart' as path;

/// Recursively look for files ending with |fileNameSuffix| in |rootPath|
/// excluding |excludeDirs|.
Stream<File> findFilesAndFilter(
    String rootPath, String fileNameSuffix, List<String> excludeDirs,
    {bool followLinks: false}) async* {
  final Queue<Directory> dirsToList =
      new Queue<Directory>.from([new Directory(rootPath)]);
  while (dirsToList.isNotEmpty) {
    final Stream<FileSystemEntity> dirList = dirsToList
        .removeLast()
        .list(recursive: false, followLinks: followLinks);
    await for (final FileSystemEntity entity in dirList) {
      if (entity is Directory) {
        if (!excludeDirs.any((exclude) => entity.path.endsWith(exclude))) {
          dirsToList.add(entity);
        }
        continue;
      }
      if (entity is File && entity.path.endsWith(fileNameSuffix)) {
        yield entity;
      }
    }
  }
}

/// Create the ancestor directories of a file path if they do not already exist.
Future<Directory> ensureDirectoryExists(String filePath,
    {bool isDirPath: false}) async {
  String dirPath = isDirPath ? filePath : path.dirname(filePath);
  Directory dir = new Directory(dirPath);
  if (!await dir.exists()) {
    await dir.create(recursive: true);
  }
  return dir;
}

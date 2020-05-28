// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:io';

import 'package:test/test.dart';
import 'package:blobstats/blob.dart';
import 'package:blobstats/blobstats.dart';
import 'package:blobstats/package.dart';

void main() {
  group('blobstats tests', () {
    test('generate valid blobs.csv', () async {
      var tmpDir = Directory.systemTemp.createTempSync();
      var stats = BlobStats(tmpDir, tmpDir, '');

      var blob1 = Blob()
        ..hash = '123abc'
        ..sourcePath = 'entry1'
        ..size = 123
        ..count = 1;
      var blob2 = Blob()
        ..hash = 'zyx456'
        ..sourcePath = 'entry2'
        ..size = 16
        ..count = 4;
      var blob3 = Blob()
        ..hash = '1x1x1x'
        ..sourcePath = '/path/file'
        ..size = 256
        ..count = 2;

      stats.blobsByHash[blob1.hash] = blob1;
      stats.blobsByHash[blob2.hash] = blob2;
      stats.blobsByHash[blob3.hash] = blob3;

      var blobsCsvPath = await stats.csvBlobs();
      List<String> lines = File(blobsCsvPath).readAsLinesSync();

      expect(lines, hasLength(4));
      expect(lines[0], equals('Size,Share,Prop,Saved,Path'));
      expect(lines[1], equals('256,2,128,256,/path/file'));
      expect(lines[2], equals('123,1,123,0,entry1'));
      expect(lines[3], equals('16,4,4,48,entry2'));
    });

    test('generate valid packages.csv', () async {
      var tmpDir = Directory.systemTemp.createTempSync();
      var stats = BlobStats(tmpDir, tmpDir, '');

      var package1 = Package()
        ..name = 'package1'
        ..proportional = 3
        ..size = 123
        ..private = 1;
      var package2 = Package()
        ..name = 'another_package'
        ..proportional = 1
        ..size = 16
        ..private = 4;
      var package3 = Package()
        ..name = 'ABC'
        ..proportional = 12
        ..size = 256
        ..private = 2;

      stats.packages.add(package1);
      stats.packages.add(package2);
      stats.packages.add(package3);

      var packagesCsvPath = await stats.csvPackages();
      List<String> lines = File(packagesCsvPath).readAsLinesSync();

      expect(lines, hasLength(4));
      expect(lines[0], equals('Size,Prop,Private,Name'));
      expect(lines[1], equals('256,12,2,ABC'));
      expect(lines[2], equals('123,3,1,package1'));
      expect(lines[3], equals('16,1,4,another_package'));
    });
  });
}

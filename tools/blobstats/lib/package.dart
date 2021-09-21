// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(https://fxbug.dev/84961): Fix null safety and remove this language version.
// @dart=2.9

import 'package:blobstats/blob.dart';

class Package {
  String path;
  String name;
  int size;
  int proportional;
  int private;
  int blobCount;
  Map<String, Blob> blobsByPath;
}

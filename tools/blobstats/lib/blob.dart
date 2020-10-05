// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

class Blob {
  String hash;
  String sourcePath = 'Unknown';
  String buildPath;
  int size;
  int sizeOnHost;
  int count;

  int get saved {
    return size * (count - 1);
  }

  int get proportional {
    return size ~/ count;
  }
}

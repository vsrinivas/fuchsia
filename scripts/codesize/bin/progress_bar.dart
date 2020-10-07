// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// @dart = 2.8

import 'dart:core';
import 'dart:io';

/// A basic progress indicator that shows `[current/total]`, Ninja-style.
/// Updates in-place using ASCII control characters.
class ProgressBar {
  final int complete;
  int current = 0;

  ProgressBar({this.complete}) {
    stdout.write('[0/$complete]');
  }

  void update(int newValue) {
    int prevLen = '[$current/$complete]'.length;
    stdout.write('\b' * prevLen);
    stdout.write('[$newValue/$complete]');
    current = newValue;
  }

  void done() {
    stdout.writeln();
  }
}

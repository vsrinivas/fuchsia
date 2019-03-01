// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:io' show File;
import 'sl4f_client.dart';

class Audio {
  final Sl4f _sl4f;

  Audio([Sl4f sl4f]) : _sl4f = sl4f ?? Sl4f.fromEnvironment();

  /// Closes the underlying HTTP client. This need not be called if the
  /// Sl4f client is closed instead.
  void close() {
    _sl4f.close();
  }

  // TODO(perley)
  Future<void> putPlayback(File file) {
    return Future.value(null);
  }

  Future<void> startCapture() {
    return Future.value(null);
  }

  Future<AudioTrack> getCapture() {
    return Future.value(AudioTrack());
  }
}

class AudioTrack {
  // TODO: Implement a proper way to test this and a proper audio container.
  bool get isSilence => false;
}

// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

class Elevations {
  static final Elevations instance = Elevations._internal();

  factory Elevations() => instance;

  Elevations._internal();

  /// Elevations for all system overlays like Ask, Status, etc.
  static double get systemOverlayElevation => 10.0;
}

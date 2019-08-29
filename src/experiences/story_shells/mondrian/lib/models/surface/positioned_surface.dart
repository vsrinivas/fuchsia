// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter/widgets.dart';

import 'surface.dart';

/// A pair of Surface and a Rect position.
class PositionedSurface {
  /// The constructor
  PositionedSurface({this.surface, this.position});

  /// The Surface
  final Surface surface;

  /// The Position
  final Rect position;

  @override
  String toString() => 'PositionedSurface('
      'surface: $surface, position: $position)';

  @override
  bool operator ==(Object o) => o is PositionedSurface && surface == o.surface;

  @override
  int get hashCode => surface.hashCode;
}

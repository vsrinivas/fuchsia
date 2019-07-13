// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:math';

import 'sl4f_client.dart';
import 'ssh.dart';

/// Clockwise rotation of the screen.
enum Rotation {
  degrees0,
  degrees90,
  degrees180,
  degrees270,
}

class Input {
  final Ssh ssh;
  final Rotation _screenRotation;

  /// Construct an [Input] object.
  ///
  /// You can change the default [screenRotation] to compensate for.
  Input(Sl4f sl4f, [this._screenRotation = Rotation.degrees0]) : ssh = sl4f.ssh;

  /// Taps on the screen at coordinates ([coord.x], [coord.y]).
  ///
  /// Coordinates must be in the range of [0, 1000] and are scaled to the screen
  /// size on the device, and they are rotated to compensate for the clockwise
  /// [screenRotation].
  Future<bool> tap(Point<int> coord, {Rotation screenRotation}) async {
    var tcoord = _rotate(coord, screenRotation ?? _screenRotation);
    final result = await ssh.run('input tap ${tcoord.x} ${tcoord.y}');
    return result.exitCode == 0;
  }

  /// Swipes on the screen from coordinates ([from.x], [from.y]) to ([to.x],
  /// [to.y]).
  ///
  /// Coordinates must be in the range of [0, 1000] are scaled to the screen
  /// size on the device, and they are rotated to compensate for the clockwise
  /// [screenRotation]. How long the swipe lasts can be controlled with
  /// [duration].
  Future<bool> swipe(Point<int> from, Point<int> to,
      {Duration duration = const Duration(milliseconds: 300),
      Rotation screenRotation}) async {
    var tfrom = _rotate(from, screenRotation);
    var tto = _rotate(to, screenRotation);
    final result = await ssh.run('input --duration=${duration.inMilliseconds} '
        'swipe ${tfrom.x} ${tfrom.y} ${tto.x} ${tto.y}');
    return result.exitCode == 0;
  }

  /// Compensates for the given [screenRotation].
  ///
  /// If null is provided, the default specified in the constructor is used.
  Point<int> _rotate(Point<int> coord, Rotation screenRotation) {
    final rotation = screenRotation ?? _screenRotation;
    switch (rotation) {
      case Rotation.degrees0:
        return coord;
      case Rotation.degrees90:
        return Point<int>(1000 - coord.y, coord.x);
      case Rotation.degrees180:
        return Point<int>(1000 - coord.x, 1000 - coord.y);
      case Rotation.degrees270:
        return Point<int>(coord.y, 1000 - coord.x);
    }
    return coord;
  }
}

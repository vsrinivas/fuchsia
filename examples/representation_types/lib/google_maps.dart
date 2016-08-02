// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:modular/builtin_types.dart';
import 'dart:typed_data';
import 'package:modular/representation_types.dart';

/// A Google Maps directions travel mode. See:
/// https://developers.google.com/maps/documentation/directions/intro#TravelModes
class TravelMode {
  static Set<String> validModes =
      new Set<String>.from(['driving', 'walking', 'bicycling', 'transit']);
  final String mode;
  TravelMode(this.mode) {
    assert(validModes.contains(mode));
  }

  @override
  String toString() => mode;
  @override
  bool operator ==(Object other) =>
      (other is TravelMode) && (toString() == other.toString());
  @override
  int get hashCode => mode.hashCode;
  // Support JSON serialization.
  Object toJson() => mode;
}

class TravelModeBindings implements RepresentationBindings<TravelMode> {
  const TravelModeBindings();

  @override
  final String label =
      'https://developers.google.com/maps/documentation/directions/intro#TravelModes';

  @override
  TravelMode decode(Uint8List data) {
    return new TravelMode(BuiltinString.read(data));
  }

  @override
  Uint8List encode(TravelMode value) {
    return BuiltinString.write(value.mode);
  }
}

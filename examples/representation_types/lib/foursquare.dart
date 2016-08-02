// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:modular/builtin_types.dart';
import 'dart:typed_data';
import 'package:modular/representation_types.dart';

/// A Foursquare venue id. See:
/// https://developer.foursquare.com/docs/venues/venues
class Venue {
  final String id;
  const Venue(this.id);

  @override
  String toString() => id;
  @override
  bool operator ==(Object other) => (other is Venue) && (id == other.id);
  @override
  int get hashCode => id.hashCode;
}

class VenueBindings implements RepresentationBindings<Venue> {
  const VenueBindings();

  @override
  final String label = 'https://developer.foursquare.com/docs/venues/venues';

  @override
  Venue decode(Uint8List data) {
    return new Venue(BuiltinString.read(data));
  }

  @override
  Uint8List encode(Venue value) {
    return BuiltinString.write(value.id);
  }
}

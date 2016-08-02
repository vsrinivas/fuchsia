// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:modular/builtin_types.dart';
import 'dart:typed_data';
import 'package:modular/representation_types.dart';

class UriBindings implements RepresentationBindings<Uri> {
  const UriBindings();

  @override
  final String label = 'https://www.w3.org/TR/url-1/';

  @override
  Uri decode(Uint8List data) {
    return Uri.parse(BuiltinString.read(data));
  }

  @override
  Uint8List encode(Uri value) {
    return BuiltinString.write(value.toString());
  }
}

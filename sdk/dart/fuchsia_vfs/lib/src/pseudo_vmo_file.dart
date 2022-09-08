// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:typed_data';

import 'package:fidl_fuchsia_io/fidl_async.dart';
import 'package:zircon/zircon.dart';

import 'pseudo_file.dart';

// ignore_for_file: public_member_api_docs

typedef VmoFn = Vmo Function();

/// A [PseudoVmoFile] is a [VmoFile] typed [PseudoFile] whose content is read
/// from a [Vmo] dynamically produced by a supplied callback.
///
/// Each FIDL connection to a [PseudoVmoFile] calls the supplied callback once
/// and reads the content of the produced [Vmo] into a buffer. Therefore,
/// connection order is important.
///
/// Reads on each connection are separately buffered.
class PseudoVmoFile extends PseudoFile {
  final VmoFn _vmoFn;

  /// Constructor for read-only [Vmo]
  ///
  /// Resulting PseudoVmoFile returns nothing when read as a regular file.
  PseudoVmoFile.readOnly(this._vmoFn) : super.readOnly(() => Uint8List(0));

  @override
  Vmo getBackingMemory(VmoFlags flags) {
    return _vmoFn().duplicate(
        ZX.RIGHTS_BASIC | ZX.RIGHT_READ | ZX.RIGHT_MAP | ZX.RIGHT_GET_PROPERTY);
  }

  @override
  NodeInfoDeprecated describeDeprecated() {
    return NodeInfoDeprecated.withFile(FileObject());
  }

  @override
  ConnectionInfo getConnectionInfo() {
    return ConnectionInfo();
  }
}

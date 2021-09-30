// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:typed_data';

import 'package:fidl_fuchsia_io/fidl_async.dart';
import 'package:fidl_fuchsia_mem/fidl_async.dart';
import 'package:zircon/zircon.dart';

import 'pseudo_file.dart';

// ignore_for_file: public_member_api_docs

typedef VmoFn = Vmo? Function();

/// A [PseudoVmoFile] is a [VmoFile] typed [PseudoFile] whose content is read
/// from a [Vmo] dynamically produced by a supplied callback.
///
/// Each FIDL connection to a [PseudoVmoFile] calls the supplied callback once
/// and reads the content of the produced [Vmo] into a buffer. Therefore,
/// connection order is important.
///
/// Reads on each connection are seperately buffered.
class PseudoVmoFile extends PseudoFile {
  final VmoFn? _vmoFn;

  /// Constructor for read-only [Vmo]
  ///
  /// Throws Exception if _vmoFn is null.
  ///
  /// Resulting PseudoVmoFile returns nothing when read as a regular file.
  PseudoVmoFile.readOnly(this._vmoFn) : super.readOnly(() => Uint8List(0)) {
    ArgumentError.checkNotNull(_vmoFn, 'Vmo Function');
  }

  Vmo? _getVmoForDescription() {
    final Vmo? originalVmo = _vmoFn!();
    final Vmo? duplicatedVmo = originalVmo?.duplicate(
        ZX.RIGHTS_BASIC | ZX.RIGHT_READ | ZX.RIGHT_MAP | ZX.RIGHT_GET_PROPERTY);
    return duplicatedVmo;
  }

  @override
  NodeInfo describe() {
    var vmo = _getVmoForDescription();
    if (vmo != null) {
      return NodeInfo.withVmofile(
          Vmofile(vmo: vmo, offset: 0, length: vmo.getSize().size));
    }
    return NodeInfo.withFile(FileObject(event: null));
  }

  @override
  ConnectionInfo describe2(ConnectionInfoQuery query) {
    var vmo = _getVmoForDescription();
    if (vmo != null) {
      return ConnectionInfo(
          representation: Representation.withMemory(MemoryInfo(
              buffer: Range(vmo: vmo, offset: 0, size: vmo.getSize().size))));
    }
    return ConnectionInfo(representation: Representation.withFile(FileInfo()));
  }
}

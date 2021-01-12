// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:fuchsia_vfs/vfs.dart' as vfs;
import 'package:meta/meta.dart';
import 'package:zircon/zircon.dart';

import '../../vmo/vmo_writer.dart';
import '../inspect.dart';

const _kHealthNodeName = 'fuchsia.inspect.Health';

/// A concrete implementation of the [Inspect] interface.
///
/// This class is not intended to be used directly by authors but instead
/// should be used by the [Inspect] factory constructor.
class InspectImpl implements Inspect {
  Node? _root;
  Vmo? _vmo;
  HealthNode? _healthNodeSingleton;

  /// The default constructor for this instance.
  InspectImpl(vfs.PseudoDir directory, String fileName, VmoWriter writer) {
    directory.addNode(fileName, writer.vmoNode);

    _root = RootNode(writer);
    _vmo = writer.vmo;
  }

  @override
  Node? get root => _root;

  @override
  HealthNode get health =>
      _healthNodeSingleton ??= HealthNode(_root!.child(_kHealthNodeName));

  /// For use in testing only.  Create the underlying health node with a timestamp provided by
  /// [timeNanos] a function that returns a 64-bit nanosecond-resolution timestamp.
  @visibleForTesting
  HealthNode healthWithNanosForTest(Function timeNanos) =>
      _healthNodeSingleton ??= HealthNode.withTimeNanosForTest(
          _root!.child(_kHealthNodeName), timeNanos);

  /// For use in testing only. There's probably no way to put @visibleForTesting
  /// because this needs to be used by the Validator Puppet, outside the current
  /// library.
  /// @nodoc
  @override
  Handle? get vmoHandleForExportTestOnly =>
      _vmo!.duplicate(ZX.RIGHT_READ | ZX.RIGHTS_BASIC | ZX.RIGHT_MAP).handle;
}

// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:fuchsia_services/services.dart';
import 'package:fuchsia_services/src/dart_vm.dart';
import 'package:fuchsia_vfs/vfs.dart';
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
  late String _fileName;
  late VmoWriter _writer;
  Node? _root;
  Outgoing? _outgoing;
  HealthNode? _healthNodeSingleton;

  /// The default constructor for this instance.
  InspectImpl(VmoWriter writer, {String fileNamePrefix = 'root'}) {
    _writer = writer;
    _root = RootNode(writer);
    _fileName = fileNamePrefix.endsWith('.inspect')
        ? fileNamePrefix
        : '$fileNamePrefix.inspect';
  }

  @override
  void onDemand(String fileNamePrefix, OnDemandRootFn rootNodeCallback) {
    if (_outgoing == null) {
      throw InspectStateError(
          'Attempted to call Inspect.onDemand before serving. Ensure that Inspect.serve is called first.');
    }
    final directory = _outgoing!.diagnosticsDir();
    final fileName = Inspect.nextInstanceWithName(fileNamePrefix);
    final pseudoVmoNode = PseudoVmoFile.readOnly(() {
      final writer = VmoWriter.withSize(Inspect.vmoSize);
      rootNodeCallback(RootNode(writer));
      return writer.vmo;
    });

    directory.addNode(fileName, pseudoVmoNode);
  }

  @override
  void serve(Outgoing outgoing) {
    // Don't repeat if serve() has already been called.
    if (_outgoing == null) {
      outgoing.diagnosticsDir().addNode(_fileName, _writer.vmoNode);
      _outgoing = outgoing;

      // If the DART VM service port has been published to the /tmp directory, attach
      //// it to the component's inpect tree.
      _root!
          .child('runner')
          ?.stringProperty('vm_service_port')
          ?.setValue(getVmServicePort() ?? '');
    } else {
      throw InspectStateError(
          'Attempted to call Inspect.serve after serving. Ensure that Inspect.serve is not called multiple times.');
    }
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
  Handle? get vmoHandleForExportTestOnly => _writer.vmo!
      .duplicate(ZX.RIGHT_READ | ZX.RIGHTS_BASIC | ZX.RIGHT_MAP)
      .handle;
}

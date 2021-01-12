// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

library topaz.public.dart.fuchsia_inspect.inspect.inspect;

import 'dart:typed_data';

import 'package:fuchsia_services/services.dart';
import 'package:fuchsia_vfs/vfs.dart';
import 'package:meta/meta.dart';
import 'package:zircon/zircon.dart';

import '../testing/util.dart' show FakeVmoHolder;
import '../vmo/vmo_writer.dart';
import 'internal/_inspect_impl.dart';

part 'node.dart';
part 'property.dart';

// ignore_for_file: public_member_api_docs

typedef OnDemandRootFn = Function(Node);

/// Unless reconfigured, the VMO will be this size.
/// @nodoc
@visibleForTesting
const int defaultVmoSizeBytes = 256 * 1024;

int _nextSuffix = 0;

/// Utility function to Generate unique names for nodes and properties.
String uniqueName(String prefix) {
  final suffix = _nextSuffix++;
  return '$prefix$suffix';
}

/// Thrown when the programmer misuses Inspect.
class InspectStateError extends StateError {
  /// Constructor
  InspectStateError(String message) : super(message);
}

/// [Inspect] exposes a structured tree of internal component state.
///
/// The [Inspect] object maintains a hierarchy of [Node] objects whose data are
/// exposed for reading by specialized tools such as iquery.
///
/// The classes exposed by this library do not support reading.
abstract class Inspect {
  /// Size of the VMO that was / will be created.
  /// @nodoc
  static int vmoSize = defaultVmoSizeBytes;
  static InspectImpl? _singleton;

  /// Maps an inspect instance name to the number of instantiations
  /// of that inspector. Used to deduplicate requests for
  /// similarly named inspectors.
  static Map<String, int>? nameToInstanceCount;

  /// For use in testing only. There's probably no way to put @visibleForTesting
  /// because this needs to be used by the Validator Puppet, outside the current
  /// library.
  /// @nodoc
  Handle? get vmoHandleForExportTestOnly;

  /// Returns a singleton [Inspect] instance at root.inspect
  factory Inspect() {
    if (_singleton == null) {
      var context = StartupContext.fromStartupInfo();
      var writer = VmoWriter.withSize(vmoSize);
      _singleton = InspectImpl(
          context.outgoing.diagnosticsDir(), 'root.inspect', writer);
    }
    return _singleton!;
  }

  /// Returns a new [Inspect] object at <name>.inspect
  /// If called multiple times with the same name within a process, a unique
  /// number will be appended, though any existing file will be overwritten.
  ///
  /// Example:
  /// Inspect.named('test');
  /// Inspect.named('test');
  /// Results in "test.inspect" and "test_2.inspect"
  factory Inspect.named(String name) {
    var context = StartupContext.fromStartupInfo();
    var writer = VmoWriter.withSize(vmoSize);
    var fileName = _nextInstanceWithName(name);
    return InspectImpl(context.outgoing.diagnosticsDir(), fileName, writer);
  }

  /// Returns a new [Inspect] object at <name>.inspect backed by a fake VMO
  /// intended for unit testing inspect integrations, so that they can run as
  /// host tests.
  factory Inspect.forTesting(FakeVmoHolder vmo, String name) {
    final writer = VmoWriter.withVmo(vmo);
    final fileName = _nextInstanceWithName(name);
    final context = StartupContext.fromStartupInfo();
    return InspectImpl(context.outgoing.debugDir(), fileName, writer);
  }

  /// Mounts an [Inspect] file at <name>.inspect whose contents are
  /// dynamically created by rootNodeCallback on each read.
  ///
  /// If methods on this class are called multiple times with the same
  /// name, a unique number will be appended to the name.
  static void onDemand(String name, OnDemandRootFn rootNodeCallback) {
    var context = StartupContext.fromStartupInfo();
    var directory = context.outgoing.diagnosticsDir();
    var fileName = _nextInstanceWithName(name);
    var pseudoVmoNode = PseudoVmoFile.readOnly(() {
      var writer = VmoWriter.withSize(vmoSize);
      rootNodeCallback(RootNode(writer));
      return writer.vmo;
    });

    directory.addNode(fileName, pseudoVmoNode);
  }

  static String _nextInstanceWithName(String name) {
    nameToInstanceCount ??= <String, int>{};
    if (nameToInstanceCount!.containsKey(name)) {
      int val = nameToInstanceCount![name]! + 1;
      nameToInstanceCount![name] = val;
      return '${name}_$val.inspect';
    } else {
      nameToInstanceCount![name] = 1;
      return '$name.inspect';
    }
  }

  /// Optionally configure global settings for inspection.
  ///
  /// This may not be called after the first call to Inspect().
  ///
  /// [vmoSizeBytes]: Sets the maximum size of the virtual memory object (VMO)
  /// used to store inspection data for this program.
  /// Must be at least 64 bytes.
  ///
  /// Throws [InspectStateError] if called after Inspect(), or [ArgumentError]
  /// if called with an invalid vmoSizeBytes.
  static void configure({int? vmoSizeBytes}) {
    if (_singleton != null) {
      throw InspectStateError(
          'configureInspect cannot be called after factory runs');
    }
    if (vmoSizeBytes != null) {
      if (vmoSizeBytes < 64) {
        throw ArgumentError('VMO size must be at least 64 bytes.');
      }
      vmoSize = vmoSizeBytes;
    }
  }

  /// The root [Node] of this Inspect tree.
  ///
  /// This node can't be deleted; trying to delete it is a NOP.
  Node? get root => _singleton!.root;

  /// The health [Node] of this Inspect tree.
  ///
  /// This node can't be deleted once created; but its creation is on demand.
  HealthNode get health => _singleton!.health;
}

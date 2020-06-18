// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';

import 'package:logging/logging.dart';

import 'sl4f_client.dart';

class Inspect {
  final Sl4f sl4f;

  /// Construct an [Inspect] object.
  // TODO(fxb/48733): make this take only Sl4f once all clients have been migrated.
  Inspect(this.sl4f);

  /// Gets the inspect data filtering using the given selectors.
  ///
  /// A selector consists of the realm path, component name and a path to a node
  /// or property.
  /// It accepts wildcards.
  /// For example:
  ///   a/*/test.cmx:path/to/*/node:prop
  ///   a/*/test.cmx:root
  ///
  /// See: https://fuchsia.googlesource.com/fuchsia/+/refs/heads/master/sdk/fidl/fuchsia.diagnostics/selector.fidl
  ///
  /// Returns an empty list if nothing is found.
  Future<List<Map<String, dynamic>>> snapshot(List<String> selectors) async {
    final hierarchyList =
        await sl4f.request('diagnostics_facade.SnapshotInspect', {
              'selectors': selectors,
            }) ??
            [];
    return hierarchyList.cast<Map<String, dynamic>>();
  }

  /// Gets the inspect data for all components currently running in the system.
  ///
  /// Returns an empty list if nothing is found.
  Future<List<Map<String, dynamic>>> snapshotAll() async {
    return await snapshot([]);
  }

  /// Gets the payload of the first found hierarchy matching the given selectors
  /// under root.
  ///
  /// Returns null if no hierarchy was found.
  Future<Map<String, dynamic>> snapshotRoot(String componentSelector) async {
    final hierarchies = await snapshot(['$componentSelector:root']);
    if (hierarchies.isEmpty) {
      return null;
    }
    final resultHierarchy = hierarchies.first;
    if (resultHierarchy['payload'] == null) {
      _log.warning('Got null payload for "$componentSelector:root". '
          'Metadata: ${resultHierarchy['metadata']}');
      return null;
    }
    return resultHierarchy['payload']['root'];
  }
}

final _log = Logger('Inspect');

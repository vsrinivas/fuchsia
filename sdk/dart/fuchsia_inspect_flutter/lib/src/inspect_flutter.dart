// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter/material.dart';
import 'package:fuchsia_inspect/inspect.dart';
import 'package:fuchsia_services/services.dart';

/// This class provides methods to convert from Flutter DiagnosticsNodes to
/// Inspect format nodes.
class InspectFlutter {
  /// Converts an diagnostics tree into a Node tree
  /// Creates a Node with Inspect data based on what
  /// information was in the DiagnosticsNode and attaches
  /// it to the parent
  @visibleForTesting
  static void inspectFromDiagnostic(DiagnosticsNode diagnostics, Node? parent) {
    // Finds the name of the widget and assigns it to the name of the node.
    String name = '';
    for (DiagnosticsNode diagNode in diagnostics.getProperties()) {
      // Used to obtain the name of the widget by checking which
      // property is titled "widget"
      if (diagNode.name == 'widget') {
        name = diagNode.value.toString();
        break;
      }
    }

    // If the name of the child does not exist log it and skip that node
    if (name == '') {
      print('Name of node cannot be found');
      return;
    }

    // Adds a hashcode to the name of the node so that the widgets with the same
    // names do not get their properties merged.
    var childNode = parent!.child('${name}_${diagnostics.hashCode}');

    // For each property, add the property to the node.
    for (DiagnosticsNode diagNode in diagnostics.getProperties()) {
      // If the property isn't null, then get the name of the property
      // and assign its value. The value of the property can be null
      // but the property itself cannot be null.
      if (diagNode.name != null) {
        childNode!
            .stringProperty(diagNode.name!)!
            .setValue(diagNode.toDescription());
      }
    }

    for (DiagnosticsNode diagNode in diagnostics.getChildren()) {
      inspectFromDiagnostic(diagNode, childNode);
    }
  }

  /// Mounts an [Inspect] file at name.inspect to the hub that exposes the root
  /// diagnostic hierarchy for this component.
  static void exposeDiagnosticsTree(Outgoing outgoing, String name) {
    Inspect()
      ..serve(outgoing)
      ..onDemand(name, (Node inspectMountRoot) {
        var diagnosticsRoot =
            WidgetsBinding.instance.renderViewElement!.toDiagnosticsNode();
        inspectFromDiagnostic(diagnosticsRoot, inspectMountRoot);
      });
  }
}

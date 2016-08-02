// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:modular_core/log.dart';
import 'package:handler/bindings.dart';
import 'package:handler/module_instance.dart';
import 'package:handler/module_runner.dart';
import 'package:modular_core/graph/graph.dart';
import 'package:modular_core/entity/schema.dart' as entity;
import 'package:modular/modular/graph.mojom.dart' as mojo;
import 'package:modular/modular/module.mojom.dart' as mojo;
import 'package:mojo/core.dart';
import 'package:parser/expression.dart';

import 'module_footprint_graph_server.dart';

/// Callback to connect to the mojo.Module service of module implementations
/// through mojo. Acts as factory for mojo.Module service proxy instances.
typedef mojo.ModuleProxy ModuleProxyFactory(ModuleInstance instance);

/// A callback to dispose of a mojo.Module service proxy instance obtained
/// through ModuleProxyFactory.
typedef void CloseModuleProxy(ModuleInstance instance, mojo.ModuleProxy proxy);

/// A callback to update the composition graph when the display output of a
/// module instance has changed.
typedef void ComposeModule(ModuleInstance instance, String displayNodeId);

/// Connects to a module implementation through the mojo.Module service and
/// handles the communication between the handler and a module implementation.
class MojoModuleRunner implements ModuleRunner {
  final Logger _log = log("handler.MojoModuleRunner");

  /// A factory for mojo.ModuleProxy instances. This indirection is done to
  /// isolate [MojoModuleRunner] from Mojo connection routines.
  final ModuleProxyFactory _createModuleProxy;

  /// A callback to call to notify when to close the module proxy.
  final CloseModuleProxy _closeModuleProxy;

  final ComposeModule _composeModuleCallback;

  /// The proxy of the module implementation.
  mojo.ModuleProxy _moduleProxy;

  /// The Graph service that hosts the module's footprint graph.
  final mojo.GraphStub _footprintGraphStub = new mojo.GraphStub.unbound();
  ModuleFootprintGraphServer _footprintGraph;

  /// Set of all labels declared in the module manifest.
  final Set<Label> _labels = new Set<Label>();

  /// There is one ModuleRunner for each ModuleInstance. ModuleInstances are
  /// created by the recipe runner for recipe steps whose inputs match the
  /// session graph. This is set once start() is called.
  ModuleInstance _instance;

  MojoModuleRunner(this._createModuleProxy, this._closeModuleProxy,
      {final ComposeModule composeModuleCallback})
      : _composeModuleCallback = composeModuleCallback;

  /// Called by the handler to connect to the module implementation using
  /// mojo. Sends the label shorthand map and the footprint graph service.
  @override
  void start(final ModuleInstance instance) {
    assert(_instance == null);
    _log.info("Running module instance $instance");
    _instance = instance;

    _moduleProxy = _createModuleProxy(_instance);
    _moduleProxy.ctrl.errorFuture.then((dynamic value) {
      _log.severe('Failed for verb "${instance.step.verb}": $value'
          ' Maybe the service name is incorrect?');
    });

    // The module low-level API specifies that the module receives a table of
    // all labels that it may use in its first call. Here, we collect all labels
    // used by the module and create such table.
    _extractLabels(_instance.manifest.input);
    _extractLabels(_instance.manifest.output);
    _extractLabels(_instance.manifest.display);
    _extractLabels(_instance.manifest.compose);

    final Map<String, String> typeTranslationTable = <String, String>{};
    for (final Label label in _labels) {
      if (label.shorthand != null && label.shorthand.isNotEmpty) {
        typeTranslationTable[label.shorthand] = label.uri.toString();
      }
    }

    _footprintGraph = new ModuleFootprintGraphServer(_instance, this);
    _footprintGraphStub.impl = _footprintGraph;
    _footprintGraphStub.ctrl.onError = _onFootprintGraphError;

    _moduleProxy.onInitialize(
        _footprintGraphStub,
        typeTranslationTable,
        _instance.manifest.schemas
            .map((final entity.Schema s) => s.toJsonString())
            .toList());
  }

  /// Called by the Session (through ModuleInstance) to notify of a changed
  /// footprint.
  @override
  void update() {
    assert(_footprintGraph != null);
    _footprintGraph.updateModuleGraph();
  }

  /// Extracts all labels used by this module.
  void _extractLabels(final List<PathExpr> pathExpressions) {
    for (final PathExpr pathExpr in pathExpressions) {
      for (final Property property in pathExpr.properties) {
        _labels.addAll(property.labels);
        _labels.addAll(property.representations);
      }
    }
  }

  void _onFootprintGraphError(final MojoEventHandlerError e) {
    _log.severe("ModuleFootprintGraphServer error: $e\n${e?.stacktrace}");
  }

  /// Called to notify the composition graph of changed display outputs. This
  /// finds the display node among the output of the module. It is called
  /// whenever a module implementation sends output through
  /// ModuleFootprintGraphServer.applyMutations(), as there might be display
  /// output changes among it. This is invoked after the output changes are
  /// actually applied to the session graph (cf. graph_service.dart), so the
  /// composition tree can find the display node in the session graph.
  void composeDisplayOutput() {
    if (_composeModuleCallback == null) {
      return;
    }

    assert(_moduleProxy.ctrl.isBound);

    // Find display node in module output edges and call composeCallback with
    // it.
    final Iterable<Edge> displayEdges = _instance.outputEdges.where(
        (final Edge edge) =>
            edge.target.getValue(Binding.displayNodeLabel) != null);

    if (displayEdges.isEmpty) {
      return;
    }

    // We inject only one display edge into the data graph when the module is
    // capable of showing UI.
    if (displayEdges.length > 1) {
      // TODO(mesch): Find out whether this can happen in a legitimate recipe,
      // or is a bug in the module runner. I'm not sure.
      _log.severe("${_instance.manifest.url} has more than one display edge:");
      for (final Edge edge in displayEdges) {
        _log.severe(" $edge");
      }
    }

    final Node displayNode = displayEdges.first.target;
    _composeModuleCallback(_instance, displayNode.id.toString());
  }

  /// Tears down the module instance proxy and closes all active graph service
  /// instances.
  @override
  void stop() {
    _footprintGraphStub.close();

    // Turn down the module instance proxy.
    if (_closeModuleProxy != null) {
      _closeModuleProxy(_instance, _moduleProxy);
    }
  }
}

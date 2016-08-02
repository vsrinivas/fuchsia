// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';

import 'package:modular_core/graph/graph.dart' show GraphEvent;
import 'package:modular_core/entity/schema.dart';
import 'package:modular_core/util/timeline_helper.dart';
import 'package:mojo/core.dart';

import '../builtin_types.dart';
import '../graph/mojo/remote_async_graph.dart';
import '../modular/graph.mojom.dart' as mojo;
import '../modular/module.mojom.dart' as mojo;
import '../mojo_module.dart';
import '../state_graph.dart';

/// Implementation of the mojo module interface that wraps the session graph in
/// graph manager and defers to the delegate for handling onChange()
/// notifications.
class ModuleImpl implements mojo.Module {
  final MojoModule _delegate;
  Map<String, String> _labelShorthandToUrl;
  StateGraph _stateGraph;

  /// Tag that identifies the module for logging / tracing purposes. Ideally
  /// this would be a url of the module we get in acceptConnection, but Flutter
  /// doesn't seem to expose that. We therefore rely on FlutterModule to set
  /// this by other (hacky) means.
  String moduleTag;

  ModuleImpl(final MojoMessagePipeEndpoint endpoint, this._delegate,
      {this.moduleTag}) {
    new mojo.ModuleStub.fromEndpoint(endpoint, this);
    registerBuiltinTypes(bindingsRegistry);
  }

  @override
  Future<Null> onInitialize(
      final mojo.GraphInterface moduleGraph,
      final Map<String, String> labelShorthandToUrl,
      final List<String> jsonSchemas) async {
    return traceAsync("ModuleImpl $_debugTag onInitialize", () async {
      _labelShorthandToUrl = labelShorthandToUrl;

      for (final String json in jsonSchemas) {
        final Schema parsed = new Schema.fromJsonString(json);

        // Also register any shorthands as aliases for this Schema.
        final List<String> aliases = _labelShorthandToUrl.keys
            .where((final String k) => _labelShorthandToUrl[k] == parsed.type)
            .toList();
        // Publish to the global registry. The global registry will
        // contain only those Schemas passed in here since we are running
        // in our own Mojo app.
        SchemaRegistry.global.add(parsed, aliases);
      }
      _delegate.onInitialize();

      // Populate StateGraph.
      final RemoteAsyncGraph graph =
          new RemoteAsyncGraph(moduleGraph as mojo.GraphProxy);
      graph.metadata.debugName = 'Module Footprint Graph: $_debugTag';
      await graph.waitUntilReady();
      graph.addObserver(_onFootprintGraphChanged);

      _stateGraph =
          new StateGraph(graph, _labelShorthandToUrl, moduleTag: moduleTag);

      // Notify the module for the first event.
      return _onFootprintGraphChanged();
    });
  }

  Future<Null> _onFootprintGraphChanged([final GraphEvent event]) async {
    try {
      await _delegate.onChange(_stateGraph);
    } catch (exception, stacktrace) {
      print('Exception while running module $_debugTag onChange: $exception');
      print(stacktrace);
    }
  }

  String get _debugTag => moduleTag ?? '${_delegate.runtimeType}';
}

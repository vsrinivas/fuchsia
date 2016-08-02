// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';
import 'dart:developer';

import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:flutter/widgets.dart';
import 'package:modular/modular/compose.mojom.dart';
import 'package:modular/modular/module.mojom.dart';
import 'package:modular/mojo_module.dart';
import 'package:modular/src/module_impl.dart';
import 'package:modular/state_graph.dart';
import 'package:modular_core/log.dart' as modular_log;
import 'package:mojo/application.dart';
import 'package:mojo/bindings.dart';
import 'package:mojo/core.dart';
import 'package:mojo_services/mojo/ui/view_token.mojom.dart';

export 'package:modular/mojo_module.dart';
export 'package:modular/state_graph.dart' show SemanticNode;

typedef ModularStatefulWidget ModularWidgetBuilder(FlutterModule module);

/// Interface for Modules that display and/or compose Flutter [Widget]s.
// ignore: always_specify_types
class FlutterModule extends MojoModule {
  final ModularWidgetBuilder _widgetBuilder;

  /// The currently active [ModularState].
  // TODO(ianloic): should this be a List?
  ModularState<ModularStatefulWidget> _widgetState;

  /// Buffered access to the session graph.
  final _BufferedStateGraph _sessionBuffer = new _BufferedStateGraph();

  /// The current embodiment.
  String embodiment;

  /// The flutter widget for this module.
  ModularStatefulWidget _widget;

  /// Compose data that's buffered until a widget is displayed.
  Iterable<ModuleComposeData> _bufferComposeChildModulesData;

  /// Implementation of the module mojom.
  ModuleImpl _moduleImpl;

  FlutterModule() : _widgetBuilder = null {
    _initialize();
  }

  FlutterModule.withState(final ModularStateBuilder stateBuilder)
      : _widgetBuilder = ((FlutterModule module) =>
            new _ModularStatefulWidget(module, stateBuilder)) {
    _initialize();
  }

  void _initialize() {
    Timeline.timeSync('$runtimeType _initialize()', () {
      // The |MojoShell| exposed by flutter framework gets initialized only when
      // flutter bindings are initialized. Explicit initialization will ensure
      // that the 'shell' object is available for use before rendering begins.
      WidgetsFlutterBinding.ensureInitialized();
      assert(shell != null);

      final _ComposerImpl composerImpl = new _ComposerImpl(this);
      final _ComposableImpl composableImpl = new _ComposableImpl(this);

      shell.provideService(Module.serviceName,
          (MojoMessagePipeEndpoint endpoint) {
        _moduleImpl = new ModuleImpl(endpoint, this);
      });

      // TODO(alhaad): This service must only be provided by modules which
      // compose other modules.
      shell.provideService(Composer.serviceName,
          (MojoMessagePipeEndpoint endpoint) {
        new ComposerStub.fromEndpoint(endpoint, composerImpl);
      });

      shell.provideService(Composable.serviceName,
          (MojoMessagePipeEndpoint endpoint) {
        new ComposableStub.fromEndpoint(endpoint, composableImpl);
      });
    });
  }

  /// Create the flutter widget for this module.
  ModularStatefulWidget createWidget() {
    final ModularStatefulWidget widget = _widgetBuilder(this);
    _moduleImpl.moduleTag = widget.runtimeType.toString();
    return widget;
  }

  @override // MojoModule
  Future<Null> onChange(StateGraph stateGraph) async {
    _sessionBuffer.stateGraph = stateGraph;
    if (_widget == null) {
      // We have the session graph now, create the widget.
      _widget = createWidget();
    }
    if (_widgetState != null) {
      _sendStateGraphToState();
    }
  }

  /// Send the current [StateGraph] to the current [State].
  void _sendStateGraphToState() {
    assert(_widgetState != null);
    assert(_sessionBuffer.stateGraph != null);
    _widgetState.rebuild(() {
      _widgetState._sessionBuffer.stateGraph = _sessionBuffer.stateGraph;
    });
    _widgetState.onChange();
  }

  Future<Null> updateSession(SessionUpdater sessionUpdater) =>
      _sessionBuffer.updateSession(sessionUpdater);

  /// Run the setter. If a flutter state object exists run it in the context of
  /// that flutter state object so that the UI will be redrawn.
  void setState(VoidCallback setter) {
    if (_widgetState != null) {
      _widgetState.rebuild(setter);
    } else {
      setter();
    }
  }

  /// Invoked when the Module *may* be displayed. The Module must return a
  /// [Widget] suitable to the given embodiment (which was registered in the
  /// manifest).
  Widget onDisplay(String embodiment) {
    print('$runtimeType<${_widgetState.runtimeType}>.onDisplay($embodiment)');
    setState(() {
      this.embodiment = embodiment;
    });
    if (_widget == null) {
      _widget = createWidget();
    }
    return _widget;
  }

  Future<bool> onBack() async =>
      _widgetState != null ? _widgetState.onBack() : false;

  /// Invoked when other Modules are available to be composed by this Module.
  /// |childModulesData| is a list of module instance data from which widgets
  /// for the embodiment can be derived.
  ///
  /// TODO(armansito): A new version of onCompose should work with the new
  /// StateGraph class instead of relying on the input/output fields in
  /// ModuleInstanceDisplayData (which should go away).
  // TODO(alhaad): onCompose should go away in favour of addChild() and
  // removeChild();
  void onCompose(Iterable<ModuleComposeData> childModulesData) {
    if (_widgetState == null) {
      _bufferComposeChildModulesData = childModulesData;
      return;
    }
    _widgetState.onCompose(childModulesData);
  }

  Proxy<dynamic> requestService(ServiceConnectionCallback callback) {
    // Passing null to the Shell requests the service from the "embedder";
    // Modular arranges for this to obtain the service from the Handler.
    return shell.connectToApplicationService(null, callback);
  }

  void _setWidgetState(ModularState<ModularStatefulWidget> widgetState) {
    this._widgetState = widgetState;
    if (widgetState != null && _sessionBuffer.stateGraph != null) {
      _sendStateGraphToState();
      if (_bufferComposeChildModulesData != null) {
        widgetState.onCompose(_bufferComposeChildModulesData);
        _bufferComposeChildModulesData = null;
      }
    }
  }
}

class _ComposerImpl implements Composer {
  final modular_log.Logger _log = modular_log.log('_ComposerImpl');

  final FlutterModule _flutterModule;
  final Map<String, ModuleInstanceDisplayData> _composedChildrenData =
      <String, ModuleInstanceDisplayData>{};
  final Map<String, ChildView> _composedViews = <String, ChildView>{};
  final Map<String, String> _displayNodeIds = <String, String>{};

  _ComposerImpl(this._flutterModule);

  @override // Composer
  void addChild(String id, ModuleInstanceDisplayData data) {
    // The |connectToApplication| call here neither creates a new instance of
    // the child module, nor does it connect to an existing instance.
    // Creation of the child instance was done in composition_tree.dart when it
    // called createView().
    final ApplicationConnection connection =
        shell.connectToApplication(data.url);
    _composedChildrenData[id] = data;
    _composedViews[id] = new ChildView(
        child: new ChildViewConnection.fromViewOwner(
            viewOwner: data.viewOwner, connection: connection));
    _composeChildren();
  }

  @override // Composer
  void removeChild(String id) {
    final ComposableProxy composableProxy =
        _composedChildrenData[id].childInterface;
    composableProxy.close();
    _composedViews.remove(id);

    final ViewOwnerProxy viewOwnerProxy = _composedChildrenData[id].viewOwner;
    _composedChildrenData.remove(id);
    _composeChildren();

    viewOwnerProxy.close();
  }

  @override // Composer
  void updateChild(String id, String displayNodeId) {
    _log.info('updateChild(id:$id, displayNodeId:${_displayNodeIds[id]}'
        ' -> $displayNodeId)');

    if (_displayNodeIds[id] == displayNodeId) {
      // TODO(mesch): updateChild() should not be called when the display node
      // id has not changed, but alas it sometimes is.
      _log.warning('updateChild(id:$id, displayNodeId:$displayNodeId):'
          ' displayNodeId has not changed');
    }

    _displayNodeIds[id] = displayNodeId;
    _composeChildren();
  }

  void _composeChildren() {
    final List<ModuleComposeData> compositionData = <ModuleComposeData>[];
    for (final String id in _composedChildrenData.keys) {
      final ModuleInstanceDisplayData moduleData = _composedChildrenData[id];
      compositionData.add(new ModuleComposeData(
          moduleData,
          _displayNodeIds[id],
          new Map<String, Widget>.fromIterable(moduleData.embodiments,
              value: (String e) {
            return _composedViews[id];
          }),
          moduleData.childInterface));
    }

    _flutterModule.onCompose(compositionData);
  }
}

class _ComposableImpl implements Composable {
  final FlutterModule _flutterModule;

  String _embodiment;

  _ComposableImpl(this._flutterModule);

  @override // Composable
  void display(String embodiment) {
    if (embodiment == _embodiment) {
      // This module is already displaying the right embodiment. Nothing to do.
      return;
    }
    Widget widget = _flutterModule.onDisplay(embodiment);
    if (widget == null) {
      // Failed to display embodiment.
      _embodiment = null;
    } else {
      _embodiment = embodiment;
      runApp(new MaterialApp(routes: {"/": (_) => widget}));
    }
  }

  @override // Composable
  Future<Null> back(void callback(bool wasHandled)) async {
    callback(await _flutterModule.onBack());
  }
}

/// Class for holding the module data needed for the parent module during
/// composition.
class ModuleComposeData {
  final ModuleInstanceDisplayData moduleData;

  // Composable interface of the child, which can be used by the parent to call
  // Display() on to get whatever embodiment it'd like.
  final ComposableProxy composableProxy;

  /// The map of embodiment strings to widget to use while composing the module.
  final Map<String, Widget> embodimentMap;

  // The id of the display node in the session graph of the child.
  final String displayNodeId;

  const ModuleComposeData(this.moduleData, this.displayNodeId,
      this.embodimentMap, this.composableProxy);

  bool get isNew => displayNodeId == null;

  String toString() =>
      '$runtimeType(moduleData: $moduleData, embodimentMap: $embodimentMap)';
}

abstract class ModularStatefulWidget extends StatefulWidget {
  final FlutterModule module;

  ModularStatefulWidget(FlutterModule module)
      : module = module,
        super(key: new GlobalObjectKey(module));

  @override
  State<ModularStatefulWidget> createState();
}

class _ModularStatefulWidget extends ModularStatefulWidget {
  final ModularStateBuilder builder;
  _ModularStatefulWidget(FlutterModule module, this.builder) : super(module);

  @override
  State<ModularStatefulWidget> createState() => builder();
}

typedef void SessionUpdater(SemanticNode session);

class _DeferredSessionUpdater {
  final SessionUpdater _graphUpdater;
  final Completer<Null> _completer;
  _DeferredSessionUpdater(this._graphUpdater)
      : _completer = new Completer<Null>();
  Future<Null> get future => _completer.future;
  void call(StateGraph stateGraph) {
    _graphUpdater(stateGraph.root);
    _completer.complete(stateGraph.push());
  }
}

typedef ModularState<ModularStatefulWidget> ModularStateBuilder();

abstract class ModularState<T extends ModularStatefulWidget> extends State<T> {
  _BufferedStateGraph get _sessionBuffer => config.module._sessionBuffer;

  void onChange() {}
  void onCompose(Iterable<ModuleComposeData> childModulesData) {}
  Future<bool> onBack() async => Navigator.pop(context);

  // TODO(ianloic): somehow make this a read-only view?
  SemanticNode get session => _sessionBuffer.stateGraph?.root;

  Iterable<SemanticNode> get anchors => _sessionBuffer.stateGraph?.anchors;

  StateGraph get state => _sessionBuffer.stateGraph;

  Future<Null> updateSession(SessionUpdater sessionUpdater) =>
      _sessionBuffer.updateSession(sessionUpdater);

  String get embodiment => config.module.embodiment;

  // TODO(armansito): Probably rename the 'suggestion' to 'preview'.
  bool get runningAsPreview => embodiment == 'suggestion';

  void rebuild(VoidCallback callback) => setState(callback);

  @override
  void initState() {
    super.initState();
    config.module._setWidgetState(this);
  }

  @override
  void dispose() {
    config.module._setWidgetState(null);
    super.dispose();
  }
}

abstract class SimpleModularState extends ModularState<ModularStatefulWidget> {}

/// Buffered access to the session graph.
class _BufferedStateGraph {
  // The state graph
  StateGraph _stateGraph;

  // Session updates that couldn't be applied because the graph wasn't
  // available yet.
  final List<_DeferredSessionUpdater> _deferredSessionUpdates =
      new List<_DeferredSessionUpdater>();

  // A deferred call to actually push the session graph to the handler.
  Future<Null> _stateGraphPusher;

  // The method that's used to push the session graph.
  void _pushStateGraph() {
    print("Pushing state graph.");
    _stateGraphPusher = null;
    _stateGraph.push();
  }

  StateGraph get stateGraph => _stateGraph;

  set stateGraph(StateGraph stateGraph) {
    _stateGraph = stateGraph;
    if (_deferredSessionUpdates.length == 0) {
      // No updates to apply.
      return;
    }
    print("Applying ${_deferredSessionUpdates.length} deferrred updates");
    // Run each of the updaters against the state graph.
    for (_DeferredSessionUpdater updater in _deferredSessionUpdates) {
      updater(_stateGraph);
    }
    _deferredSessionUpdates.clear();
    // Schedule the state graph to be pushed, if a push isn't already scheduled.
    if (_stateGraphPusher == null) {
      _stateGraphPusher = new Future<Null>(_pushStateGraph);
    }
  }

  Future<Null> updateSession(SessionUpdater sessionUpdater) {
    if (_stateGraph == null) {
      // No [StateGraph] yet. Defer changes.
      _deferredSessionUpdates.add(new _DeferredSessionUpdater(sessionUpdater));
      return _deferredSessionUpdates.last.future;
    }
    sessionUpdater(_stateGraph.root);

    // Schedule the state graph to be pushed, if a push isn't already scheduled.
    if (_stateGraphPusher == null) {
      _stateGraphPusher = new Future<Null>(_pushStateGraph);
    }
    return new Future<Null>.value();
  }
}

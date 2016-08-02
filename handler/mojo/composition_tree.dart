// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';

import 'package:collection/collection.dart';
import 'package:modular_core/log.dart';
import 'package:handler/constants.dart';
import 'package:handler/inspector_json_server.dart';
import 'package:handler/module_instance.dart';
import 'package:modular_core/graph/graph.dart';
import 'package:modular/modular/compose.mojom.dart' as module;
import 'package:modular/modular/module.mojom.dart' as module;
import 'package:modular_core/uuid.dart' show Uuid;
import 'package:mojo/application.dart';
import 'package:mojo/bindings.dart' as bindings;
import 'package:mojo/core.dart';
import 'package:mojo/mojo/service_provider.mojom.dart';
import 'package:mojo_services/input/input.mojom.dart';
import 'package:mojo_services/mojo/ui/view_provider.mojom.dart';
import 'package:mojo_services/mojo/ui/view_token.mojom.dart';
import 'package:parser/expression.dart' show PathExpr, Label, Property;

import 'handler_application.dart';

// _CompositionNode is a node in a |CompositionTree| and encapsulates
// information / connection about a (display) module instance.
// TODO(ksimbili): Make this a subclass of Graph::Node.
class _CompositionNode {
  _CompositionNode composingParent;

  List<_CompositionNode> children = <_CompositionNode>[];

  _CompositionNode(this.instance);

  // URL of the mojo app this node connects to.
  Uri get url => instance.manifest.url;

  final Completer<Null> interfaceConnectionCompleter = new Completer<Null>();

  // |ServiceProviderProxy| for the corresponding module instance. We need to
  // store this as we want to fetch the composable proxy when ever we reparent a
  // module.
  final ServiceProviderProxy remoteServices =
      new ServiceProviderProxy.unbound();

  // |CompositionTree| connects to the application at |url| and  passes the
  // handle to module proxy back via addModule().
  final module.ModuleProxy moduleProxy = new module.ModuleProxy.unbound();

  // |CompositionTree| connects to this node and uses it to tell the composing
  // parent to add / remove nodes.
  final module.ComposerProxy composerProxy = new module.ComposerProxy.unbound();

  // |CompositionTree| connects to the application at |url| and  passes the
  // handle to composable proxy and view owner proxy to the composer composing
  // this node.
  // The view owner proxy of the root module will be maintained.
  _ViewOwnerConnector viewOwnerConnector = new _ViewOwnerConnector();

  // Instance correspoding to the composition node.
  final ModuleInstance instance;

  // The id of the node in the session graph which represents the display node
  // corresponding to this _CompositionNode. Note that there is exactly one
  // display node in the graph corresponding to each _CompositionNode in the
  // CompositionTree.
  String displayNodeId;

  @override
  String toString() => '_CompositionNode<$url parent=$composingParent>';
}

/// A tree representing the current composition of [Module]s.
///
/// The [CompositionTree] is responsible for:
///
/// 1. Converting the current running [Graph] into its corresponding composition
///    tree representation.
/// 2. Maintaining a connection to the view system and keeping it up to date
///    with the current composition.
class CompositionTree implements ViewProvider, Inspectable {
  final Logger _log = log("handler.CompositionTree");

  /// Map to maintain all nodes in the tree with id of the instance
  /// corresponding to the node as the key.
  final Map<int, _CompositionNode> _instanceToNode = <int, _CompositionNode>{};

  // A list of all node ids which are waiting because their composing parent
  // is not known and they are not root nodes.
  final List<int> _waitingInstances = <int>[];

  final HandlerRunner _handlerRunner;

  final InspectorJSONServer _inspector;

  // The node being displayed at the root view.
  _CompositionNode _rootNode;

  // The stub of the view owner requested by Mozart and implemented by the
  // module running at the root view.
  ViewOwnerStub _mozartViewOwnerStub;

  CompositionTree(this._handlerRunner, this._inspector) {
    _inspector?.publish(this);
  }

  /// We define the following composition rules which are followed while adding
  /// a composition node to the composition tree.
  ///
  /// Choosing the parent for the current composition node:
  ///
  /// 1. Find all nodes which can compose the current node, by checking if the
  ///    current node resolves to the 'compose' field.
  ///
  /// 2. Find if any of those nodes is a parent in the data-flow graph. If so,
  ///    such nodes are give higher priority to compose the current node.
  ///
  /// 3. Choose a node from final filtered list of nodes as a parent for the
  ///    current node. This choosing is bit arbitrary. May be we should define
  ///    how to choose (could be based on age of the module, z-index, etc.).
  ///
  /// Choosing children:
  ///
  /// If we define how to choose a node in the step 3 above, then we need to
  /// collect children for the current node from the composition tree, as the
  /// order could change based on the criteria we use in step 3 above.
  module.ModuleProxy addModule(
      final ModuleInstance instance, String rootEmbodiment) {
    assert(_instanceToNode[instance.id] == null);
    final _CompositionNode node = new _CompositionNode(instance);
    _addNode(node);
    _connect(node);

    // Root view.
    if (_rootNode == null) {
      assert(rootEmbodiment != null && rootEmbodiment.isNotEmpty);
      _rootNode = node;
      // TODO(ksimbili): Figure out a way to close rootComposableProxy.
      module.ComposableProxy rootComposableProxy = _getComposableProxy(node);
      _BackButton.connectToModule(
          _handlerRunner.connectToApplication, rootComposableProxy);
      rootComposableProxy.display(rootEmbodiment);
      return node.moduleProxy;
    }

    if (node.composingParent == null) {
      assert(!_waitingInstances.contains(instance.id));
      _waitingInstances.add(instance.id);
      return node.moduleProxy;
    }

    _composeNode(node);
    _addWaitingInstancesToTree();

    _inspector?.notify(this);

    return node.moduleProxy;
  }

  void removeModule(final ModuleInstance instance) {
    assert(_instanceToNode[instance.id] != null);
    final _CompositionNode node = _instanceToNode[instance.id];
    node.moduleProxy.close();
    node.composerProxy.close();

    _instanceToNode.remove(instance.id);
    _waitingInstances.remove(instance.id);

    node.children.forEach((final _CompositionNode child) {
      // Remove child from the parent.
      node.composerProxy.removeChild(child.instance.id.toString());
      child.composingParent = null;
      // Add child to the waiting instances list.
      assert(!_waitingInstances.contains(child.instance.id));
      _waitingInstances.add(child.instance.id);
      // Reset the viewOwnerConnector stub so that it can be used with a new
      // parent.
      child.viewOwnerConnector.resetStub();
    });

    if (node.composingParent != null) {
      node.viewOwnerConnector.close();
      node.composingParent.composerProxy
          .removeChild(node.instance.id.toString());
    }

    _inspector?.notify(this);
  }

  void updateModule(final ModuleInstance instance, final String displayNodeId) {
    assert(_instanceToNode[instance.id] != null);
    final _CompositionNode node = _instanceToNode[instance.id];
    node.displayNodeId = displayNodeId;
    if (node.composingParent != null) {
      node.composingParent.composerProxy
          .updateChild(instance.id.toString(), displayNodeId);
    } else {
      // TODO(mesch): Find out whether this can happen in a legitimate recipe,
      // or whether that's a bug. I'm not quite sure.
      _log.severe("Composing parent is null in updateModule for $instance");
    }
  }

  void _composeNode(final _CompositionNode node) {
    assert(node.composingParent != null);
    final List<String> stringDisplays = node.instance.manifest.display
        .map((PathExpr d) => d.toString())
        .toList();

    // Check that node is not composed by any other parent.
    assert(!node.viewOwnerConnector.stub.ctrl.isBound);

    // TODO(ksimbili/armansito): Fill graph which will be used by the composing
    // parent.
    // TODO(alhaad): Understand what the above comment means.
    // TODO(armansito): Help alhaad with his TODO above.
    final module.ModuleInstanceDisplayData displayData =
        new module.ModuleInstanceDisplayData()
          ..url = node.url.toString()
          ..viewOwner = node.viewOwnerConnector.stub
          ..childInterface = _getComposableProxy(node)
          ..embodiments = stringDisplays;

    // Include the live suggestion ID in the metadata, so that the composing
    // system module/UI can associate a live suggestion node with the suggestion
    // itself.
    final Uuid liveSuggestionId =
        node.instance.session.metadata.getLiveSuggestionId();
    if (liveSuggestionId != null) {
      displayData.liveSuggestionId = liveSuggestionId.toBase64();
    }

    node.composingParent.composerProxy
        .addChild(node.instance.id.toString(), displayData);

    if (node.displayNodeId != null) {
      updateModule(node.instance, node.displayNodeId);
    }

    _inspector?.notify(this);
  }

  void _addWaitingInstancesToTree() {
    bool addedANewInstance = true;
    while (addedANewInstance) {
      addedANewInstance = _waitingInstances.any((final int instanceId) {
        final _CompositionNode waitingNode = _instanceToNode[instanceId];
        // Try again adding the waiting instances to find the composing parent.
        _addNode(waitingNode);
        if (waitingNode.composingParent != null) {
          // Found the parent.
          _composeNode(waitingNode);
          _waitingInstances.remove(instanceId);
          return true;
        }
        return false;
      });
    }
  }

  void _addNode(final _CompositionNode node) {
    // TODO(ksimbili): We are doing simple matching of 'display'.
    // Instead, we should be resolving the compose field as mentioned in the
    // composition rules step #1.
    // TODO(ksimbili): Matching of display to compose shouldn't happen based on
    // string, rather by evaluating pathExpr on the graph.
    // TODO(armansito|ksimbili): A suggestion node should never be the composing
    // parent of other modules that are not part of the same speculative
    // execution context (i.e. part of the same temporary fork session).
    for (final _CompositionNode n in _instanceToNode.values) {
      if (_waitingInstances.contains(n.instance.id)) {
        // The node is already waiting, so we cannot attach to it.
        continue;
      }
      // HACK, we match for display expressions in compose by ignoring
      // cardinality to find the composing parent. Eventually, we won't need all
      // this and the data flow would determine the parent-child relationships.
      // TODO(ksimbili): Remove this hack when appropriate.
      if (_matchesIgnoringCardinality(n.instance, node.instance)) {
        node.composingParent = n;
        n.children.add(node);
        // TODO(alhaad): This means that a module instance can only be
        // embedded by a single parent. Break this contraint after FLutter
        // supports a ViewOwner based interface.
        break;
      }
    }

    if (node.composingParent == null) {
      _log.severe(
          'No composing parent found for module instance ${node.instance} with '
          'display - ${node.instance.manifest.display}');
    }
    _instanceToNode[node.instance.id] = node;
  }

  /// Returns if all display expressions in the display instance ignoring
  /// cardinality match compose expressions in the compose instance. The compose
  /// and display expressions are found by resolving the display and compose
  /// expressions of the manifests in the anchor expressions of the step.
  bool _matchesIgnoringCardinality(final ModuleInstance composeInstance,
      final ModuleInstance displayInstance) {
    // TODO(armansito|mesch): Currently we require that the displays in
    // |displayInstance| are all be composable by |composeInstance|. We should
    // relax the matching requirements here. Until then, we are special casing
    // the suggestion embodiment here with the following rules:
    //
    //    1. If |displayInstance| is from a live suggestion then it has to
    //    declare the suggestion embodiment.
    //
    //    2. If |displayInstance| is from a live suggestion, then we only match
    //    based on the "suggestion" embodiment. However, if |displayInstance| is
    //    NOT from a live suggestion but it declares the "suggestion" embodiment
    //    in its manifest, we explicitly DON'T try to match that specific
    //    embodiment.
    //
    //    3. All modules that declare the "suggestion" embodiment and come from
    //    a live suggestion should automatically be parented to the launcher. It
    //    is up to the launcher to group multiple ChildViews from the same
    //    suggestion together based on the suggestion ID and compose them in a
    //    meaningful manner. (This can happen if a live-suggestion module has
    //    multiple instances.
    final bool isSuggestionNode =
        displayInstance.session.metadata.getLiveSuggestionId() != null;
    List<PathExpr> displays;
    if (isSuggestionNode) {
      displays = displayInstance.manifest.display
          .where((final PathExpr e) =>
              e.containsLabelAsString(Constants.suggestionDisplayLabel))
          .toList();
      assert(displays.isNotEmpty);
    } else {
      displays = displayInstance.manifest.display
          .where((final PathExpr e) =>
              !e.containsLabelAsString(Constants.suggestionDisplayLabel))
          .toList();
    }

    final List<List<Property>> displayPaths =
        displayInstance.step.resolvePaths(displays);
    final List<List<Property>> composePaths =
        composeInstance.step.resolvePaths(composeInstance.manifest.compose);

    return displayPaths.every((final List<Property> d) {
      return composePaths.any((final List<Property> c) {
        if (d.length != c.length) {
          return false;
        }
        for (int i = 0; i < d.length; i++) {
          if (!const SetEquality<Label>().equals(d[i].labels, c[i].labels)) {
            return false;
          }
        }
        return true;
      });
    });
  }

  // Establishes connection to the module instance represented by this node.
  // All service provider exchanges with the module instance is done here.
  // TODO(alhaad): Move the connection here to flutter modules and that to
  // mojo modules to a separate connection manager class.
  void _connect(final _CompositionNode node) {
    final ApplicationConnection connection =
        _handlerRunner.connectToApplication(node.url.toString());
    ViewProviderProxy viewProviderProxy = new ViewProviderProxy.unbound();
    connection.requestService(viewProviderProxy);
    viewProviderProxy.createView(
        node.viewOwnerConnector.viewOwner, node.remoteServices);
    if (_rootNode == null && _mozartViewOwnerStub != null) {
      _mozartViewOwnerStub.impl = node.viewOwnerConnector;
    }
    viewProviderProxy.close();

    // Connect to node's Module interface.
    // TODO(alhaad): Connection should be done using verb to allow multiple
    // verb implementation by a single app.
    _connectToService(
        module.Module.serviceName, node.remoteServices, node.moduleProxy);

    // Connect to node's Composer interface.
    if (node.instance.manifest.compose.isNotEmpty) {
      _connectToService(
          module.Composer.serviceName, node.remoteServices, node.composerProxy);
    }

    node.interfaceConnectionCompleter.complete();
  }

  void _connectToService(String serviceName,
      final ServiceProviderProxy remoteServices, bindings.Proxy proxy) {
    final MojoMessagePipe proxyPipe = new MojoMessagePipe();
    proxy.ctrl.bind(proxyPipe.endpoints[0]);
    remoteServices.connectToService_(serviceName, proxyPipe.endpoints[1]);
  }

  /// Returns the [module.ComposableProxy] implemented by the module at
  /// [_CompositionNode].
  module.ComposableProxy _getComposableProxy(final _CompositionNode node) {
    module.ComposableProxy composableProxy =
        new module.ComposableProxy.unbound();
    // Connect to node's Composable interface.
    _connectToService(
        module.Composable.serviceName, node.remoteServices, composableProxy);
    return composableProxy;
  }

  @override // ViewProvider
  void createView(ViewOwnerInterfaceRequest viewOwner,
      ServiceProviderInterfaceRequest services) {
    _mozartViewOwnerStub = viewOwner;
    if (_rootNode != null) {
      _mozartViewOwnerStub.impl = _rootNode.viewOwnerConnector;
    }
  }

  @override
  Future<dynamic> inspectorJSON() async {
    // Build a tree...
    final Map<_CompositionNode, Set<_CompositionNode>> children = {};
    for (final _CompositionNode node in _instanceToNode.values) {
      final _CompositionNode parent = node.composingParent;
      if (parent == null) {
        continue;
      }
      if (!children.containsKey(parent)) {
        children[parent] = new Set<_CompositionNode>();
      }
      children[parent].add(node);
    }
    Map<String, dynamic> serialize(_CompositionNode node) {
      return {
        'instance': node.instance.inspectorPath,
        'url': node.url,
        'embodiments':
            node.instance.manifest.display.map((PathExpr d) => d.toString()),
        'children': children[node]?.map(serialize) ?? [],
      };
    }
    return {
      'type': 'compositionTree',
      'tree': _rootNode == null ? null : serialize(_rootNode)
    };
  }

  @override // Inspectable
  Future<dynamic> onInspectorPost(dynamic json) async {}

  @override
  String get inspectorPath => '/composition-tree';
}

// Implement a listener for the back button.
class _BackButton implements InputClient {
  static _BackButton _instance; // Singleton.
  final InputServiceProxy _input = new InputServiceProxy.unbound();
  final InputClientStub _stub = new InputClientStub.unbound();
  module.ComposableProxy _proxy;

  _BackButton._internal(Function connectToApplication) {
    _stub.impl = this;
    connectToApplication("mojo:input").requestService(_input);
    _input.setClient(_stub);
  }

  static void connectToModule(
      Function connectToApplication, module.ComposableProxy proxy) {
    if (_instance == null) {
      _instance = new _BackButton._internal(connectToApplication);
    }
    _instance._proxy = proxy;
  }

  @override
  void onBackButton(void callback()) {
    if (_proxy != null) {
      _proxy.back((bool wasHandled) {
        if (!wasHandled) {
          _proxy.close(); // TODO(tonyg): How to shutdown cleanly.
        }
        callback();
      });
    } else {
      callback();
    }
  }
}

// Connector to the viewOwner which caches the token and reuses it when the view
// needs to be reparented.
class _ViewOwnerConnector implements ViewOwner {
  final ViewOwnerProxy viewOwner = new ViewOwnerProxy.unbound();
  ViewToken _cachedToken;
  ViewOwnerStub _stub = new ViewOwnerStub.unbound();

  _ViewOwnerConnector() {
    _stub.impl = this;
  }

  @override // ViewOwner
  void getToken(void callback(ViewToken token)) {
    if (viewOwner.ctrl.isBound) {
      // TODO(ksimbili): We should bring back the viewOwner from flutter when
      // we remove the child, instead of caching the token and reusing it.
      viewOwner.getToken((final ViewToken token) {
        _cachedToken = token;
        callback(_cachedToken);
      });
    } else {
      callback(_cachedToken);
    }
  }

  ViewOwnerStub get stub => _stub;

  void resetStub() {
    _stub.close();
    _stub = new ViewOwnerStub.unbound()..impl = this;
  }

  void close() {
    if (!_stub.ctrl.isBound) {
      viewOwner.close();
    }
  }
}

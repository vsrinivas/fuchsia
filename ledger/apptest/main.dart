// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';
import 'dart:collection';
import 'dart:core';

import 'package:modular_core/uuid.dart';
import 'package:modular_services/ledger/ledger.mojom.dart';
import 'package:mojo/application.dart';
import 'package:mojo_apptest/apptest.dart';

import 'json_query_builder_test.dart';

class TestHelper {
  final Application _application;
  final Uri _ledgerTestUri;
  // Labels from 'A' to 'Z'.
  final Map<String, LabelUri> labels = <String, LabelUri>{};

  final LedgerProxy _ledgerProxy = new LedgerProxy.unbound();

  TestHelper(this._application, String uri) : _ledgerTestUri = Uri.parse(uri) {
    String uri = "https://tq.mojoapps.io/ledger.mojo";
    String ledgerServiceUri = _ledgerTestUri.resolve(uri).toString();
    _application.connectToService(
        ledgerServiceUri, _ledgerProxy, Ledger.serviceName);
    int asciiCode = 'A'.codeUnits[0];
    for (int i = 0; i < 26; i++) {
      final String letter = new String.fromCharCode(asciiCode);
      labels[letter] = new LabelUri()..uri = letter;
      asciiCode++;
    }
  }

  Ledger get ledger => _ledgerProxy;

  Future<Null> close() async {
    await _ledgerProxy.close();
  }

  static List<dynamic> newList(int length, dynamic newElement()) {
    final List<dynamic> list = [];
    for (int i = 0; i < length; i++) {
      list.add(newElement());
    }
    return list;
  }

  static NodeRecord newNode() {
    return new NodeRecord()
      ..nodeId = (new NodeId()..id = new Uuid.random().toBase64())
      ..representationValues = []
      ..deleted = false;
  }

  static EdgeRecord newEdge(
      NodeRecord start, List<LabelUri> labels, NodeRecord end) {
    final EdgeRecord edge = new EdgeRecord()
      ..edgeId = (new EdgeId()..id = new Uuid.random().toBase64())
      ..start = start.nodeId
      ..end = end.nodeId
      ..labels = labels
      ..deleted = false;
    return edge;
  }

  static LabelExpression newSemanticExpression(
      List<LabelUri> labels, Cardinality cardinality,
      [PathExpression next]) {
    return new LabelExpression()
      ..semantic = (new SemanticExpression()
        ..labels = labels
        ..cardinality = cardinality
        ..next = next);
  }

  static LabelExpression newRepresentationExpression(List<LabelUri> labels) {
    return new LabelExpression()
      ..representation = (new RepresentationExpression()..labels = labels);
  }

  static PathExpression newSimplePathExpression(
      List<List<LabelUri>> path, PathExpression next,
      [List<Cardinality> cardinalities]) {
    for (int i = path.length - 1; i >= 0; i--) {
      List<LabelUri> labels = path[i];
      Cardinality cardinality =
          cardinalities == null ? Cardinality.singular : cardinalities[i];
      PathExpression current = new PathExpression()
        ..expressions = [newSemanticExpression(labels, cardinality, next)];
      next = current;
    }
    return next;
  }

  /// Checks that the two lists are equal. Elements of the two lists should
  /// contain an id retrievable by the given [getId] function. Two elements are
  /// considered equal if their ids are equal.
  static void _checkListsEqual(List<dynamic> foundList,
      List<dynamic> expectedList, String getId(dynamic e)) {
    expect(foundList.length, expectedList.length,
        reason: "$foundList != $expectedList");

    // Define the equals and hashCode functions for the HashSet.
    Function _equals = (dynamic e1, dynamic e2) => getId(e1) == getId(e2);
    Function _hashCode = (dynamic e) => getId(e).hashCode;

    // Put found elements in HashSet and find there all elements in
    // the expected elements' list.
    final HashSet<dynamic> foundSet = new HashSet<dynamic>(
        equals: _equals, hashCode: _hashCode)..addAll(foundList);
    for (dynamic elem in expectedList) {
      expect(foundSet.contains(elem), true);
    }
  }

  /// Creates a new session, checks that a valid [SessionId] is returned by
  /// the Ledger and returns it.
  Future<SessionId> checkCreateSession() {
    final Completer<SessionId> completer = new Completer<SessionId>();
    ledger
        .createSession((final SessionId sessionId, final LedgerStatus status) {
      expect(status, LedgerStatus.ok);
      expect(sessionId != null, true);
      completer.complete(sessionId);
    });
    return completer.future;
  }

  /// Checks that [SessionGraph] return by the [Ledger]s [getSession] method
  /// matches the expected one.
  Future<Null> checkGetSessionGraph(
      SessionId sessionId, SessionGraph expectedGraph,
      {bool createIfMissing: false}) {
    final Completer completer = new Completer();
    ledger.getSessionGraph(
        sessionId, new LedgerOptions()..createIfMissing = createIfMissing,
        (final SessionGraph sessionGraph, final LedgerStatus status) {
      expect(status, LedgerStatus.ok);
      expect(sessionGraph != null, true);
      checkGraphsEqual(sessionGraph, expectedGraph);
      completer.complete();
    });
    return completer.future;
  }

  /// Removes the nodes and edges associated with the given ids and checks
  /// that the final lists of removed edges and nodes in the session graph match
  /// the ones given in the [expectedDeleted] graph.
  Future<Null> checkRemove(SessionId sessionId, List<NodeId> nodes,
      List<EdgeId> edges, SessionGraph expectedDeleted) async {
    final Completer<LedgerStatus> statusCompleter =
        new Completer<LedgerStatus>();
    ledger.updateSessionGraph(sessionId, [], [], [], nodes, edges,
        (final LedgerStatus status) => statusCompleter.complete(status));
    expect(await statusCompleter.future, LedgerStatus.ok);

    final Completer<SessionGraph> sessionGraphCompleter =
        new Completer<SessionGraph>();
    ledger.getSessionGraph(
        sessionId, new LedgerOptions()..createIfMissing = false,
        (final SessionGraph sessionGraph, final LedgerStatus status) {
      expect(status, LedgerStatus.ok);
      sessionGraphCompleter.complete(sessionGraph);
    });
    final SessionGraph sessionGraph = await sessionGraphCompleter.future;
    expect(sessionGraph != null, true);

    // None of the returned nodes or edges should be among the deleted ones.
    final Set<String> expectedDeletedNodes = new Set<String>.from(
        expectedDeleted.nodes.map((NodeRecord n) => n.nodeId.id));
    for (NodeRecord node in sessionGraph.nodes) {
      expect(expectedDeletedNodes.contains(node.nodeId.id), false);
    }

    final Set<String> expectedDeletedEdges = new Set<String>.from(
        expectedDeleted.edges.map((EdgeRecord e) => e.edgeId.id));
    for (EdgeRecord edge in sessionGraph.edges) {
      expect(expectedDeletedEdges.contains(edge.edgeId.id), false);
    }
  }

  static void checkGraphsEqual(SessionGraph g1, SessionGraph g2) {
    _checkListsEqual(g1.edges, g2.edges, (EdgeRecord e) => e.edgeId.id);
    _checkListsEqual(g1.nodes, g2.nodes, (NodeRecord n) => n.nodeId.id);
  }
}

class _TestObserver implements LedgerObserver {
  LedgerObserverStub _stub;
  int _nodeCount = 0;
  int _edgeCount = 0;
  int _onChangeCount = 0;

  Completer<Null> _completer;

  _TestObserver() {
    _stub = new LedgerObserverStub.unbound()..impl = this;
  }

  LedgerObserverStub get stub => _stub;

  int get nodeCount => _nodeCount;

  int get edgeCount => _edgeCount;

  int get onChangeCount => _onChangeCount;

  Future<Null> waitForChange() {
    _completer = new Completer<Null>();
    return _completer.future.timeout(new Duration(seconds: 2));
  }

  @override
  void onChange(List<GraphUpdate> changes, void callback()) {
    assert(_completer != null);

    for (final GraphUpdate change in changes) {
      if (change.tag == GraphUpdateTag.nodeAdded) {
        _nodeCount++;
      } else if (change.tag == GraphUpdateTag.edgeAdded) {
        _edgeCount++;
      }
    }
    _onChangeCount++;
    if (!_completer.isCompleted) {
      _completer.complete();
    }
    _completer = null;
    callback();
  }
}

void ledgerTests(Application app, String url) {
  TestHelper helper;

  setUp(() async {
    helper = new TestHelper(app, url);
    // A user needs to be authenticated to allow creating and editing a session.
    final Completer completer = new Completer();
    helper.ledger.authenticate(
        "test",
        (final UserId userId, final LedgerStatus status) =>
            completer.complete());
    await completer.future;
  });

  tearDown(() async {
    await helper.close();
  });

  test('Test create session, add nodes/edges and load graph', () async {
    final Map<String, LabelUri> labels = helper.labels;
    // Initial nodes and edges.
    final List<NodeRecord> nodesToAdd =
        TestHelper.newList(3, TestHelper.newNode);
    final List<EdgeRecord> edgesToAdd = <EdgeRecord>[
      TestHelper.newEdge(
          nodesToAdd[0], [labels['A'], labels['B']], nodesToAdd[1]),
      TestHelper.newEdge(
          nodesToAdd[1], [labels['C'], labels['D']], nodesToAdd[2]),
    ];
    // Additional nodes and edges.
    final List<NodeRecord> extraNodes =
        TestHelper.newList(2, TestHelper.newNode);
    final List<EdgeRecord> extraEdges = <EdgeRecord>[
      TestHelper.newEdge(extraNodes[0], [labels['A']], extraNodes[1]),
    ];

    // Create and load an empty graph.
    SessionId sessionId = await helper.checkCreateSession();
    final SessionGraph expectedGraph = new SessionGraph()
      ..edges = []
      ..nodes = [];
    await helper.checkGetSessionGraph(sessionId, expectedGraph);

    Future<LedgerStatus> _updateSessionGraph(
        List<NodeRecord> nodesToAdd,
        List<EdgeRecord> edgesToAdd,
        List<Representation> valuesToUpdate,
        List<NodeId> nodesToRemove,
        List<EdgeId> edgesToRemove) {
      final Completer<LedgerStatus> completer = new Completer<LedgerStatus>();
      helper.ledger.updateSessionGraph(
          sessionId,
          nodesToAdd,
          edgesToAdd,
          valuesToUpdate,
          nodesToRemove,
          edgesToRemove,
          (final LedgerStatus status) => completer.complete(status));
      return completer.future;
    }

    // It is not an error when any of the lists is null.
    LedgerStatus status =
        await _updateSessionGraph(null, null, null, null, null);
    expect(status, LedgerStatus.ok);
    await helper.checkGetSessionGraph(sessionId, expectedGraph);

    // Add and load an initial set of nodes and edges.
    status = await _updateSessionGraph(nodesToAdd, edgesToAdd, [], [], []);
    expect(status, LedgerStatus.ok);
    expectedGraph
      ..edges = edgesToAdd
      ..nodes = nodesToAdd;
    await helper.checkGetSessionGraph(sessionId, expectedGraph);

    // Add and load some additional nodes and edges.
    expectedGraph.nodes.addAll(extraNodes);
    expectedGraph.edges.addAll(extraEdges);
    status = await _updateSessionGraph(extraNodes, extraEdges, [], [], []);
    expect(status, LedgerStatus.ok);
    await helper.checkGetSessionGraph(sessionId, expectedGraph);
  });

  test('Test get session with createIfMissing', () async {
    // Create and load an empty graph.
    final SessionGraph expectedGraph = new SessionGraph()
      ..edges = []
      ..nodes = [];
    await helper.checkGetSessionGraph(
        new SessionId()..id = new Uuid.random().toBase64(), expectedGraph,
        createIfMissing: true);
  });

  test('Test illegal arguments', () async {
    final Map<String, LabelUri> labels = helper.labels;

    final List<NodeRecord> nodes = TestHelper.newList(2, TestHelper.newNode);
    final List<EdgeRecord> edges = <EdgeRecord>[
      TestHelper.newEdge(nodes[0], [labels['A']], nodes[1])
    ];

    SessionId sessionId = await helper.checkCreateSession();
    final SessionGraph expectedGraph = new SessionGraph()
      ..edges = []
      ..nodes = [];

    Future<LedgerStatus> _updateSessionGraph(
        List<NodeRecord> nodesToAdd,
        List<EdgeRecord> edgesToAdd,
        List<Representation> valuesToUpdate,
        List<NodeId> nodesToRemove,
        List<EdgeId> edgesToRemove) {
      final Completer<LedgerStatus> completer = new Completer<LedgerStatus>();
      helper.ledger.updateSessionGraph(
          sessionId,
          nodesToAdd,
          edgesToAdd,
          valuesToUpdate,
          nodesToRemove,
          edgesToRemove,
          (final LedgerStatus status) => completer.complete(status));
      return completer.future;
    }

    // [creationDevice] should be null. Set a different value and see the error.
    final DeviceId device = new DeviceId()..id = new Uuid.random().toBase64();
    nodes[0].creationDevice = device;
    LedgerStatus status = await _updateSessionGraph(nodes, edges, [], [], []);
    expect(status, LedgerStatus.illegalArgument);
    await helper.checkGetSessionGraph(sessionId, expectedGraph);

    // Also check for edges.
    nodes[0].creationDevice = null;
    edges[0].creationDevice = device;
    status = await _updateSessionGraph(nodes, edges, [], [], []);
    expect(status, LedgerStatus.illegalArgument);
    await helper.checkGetSessionGraph(sessionId, expectedGraph);
  });

  test('Test remove edges', () async {
/*    // Create a graph 0 -> 1, 1 -> 2, 1-> 3, 0 -> 4.
    final List<LabelUri> labels = [helper.labels['A']];
    final List<NodeRecord> nodes = TestHelper.newList(5, TestHelper.newNode);
    final List<EdgeRecord> edges = <EdgeRecord>[
      TestHelper.newEdge(nodes[0], labels, nodes[1]),
      TestHelper.newEdge(nodes[1], labels, nodes[2]),
      TestHelper.newEdge(nodes[1], labels, nodes[3]),
      TestHelper.newEdge(nodes[0], labels, nodes[4]),
    ];

    SessionId sessionId = await helper.checkCreateSession();
    await helper.ledger.updateSessionGraph(sessionId, nodes, edges, [], [], []);

    // Removing edge 0->4 shouldn't trigger any other deletions.
    SessionGraph expectedRemoved = new SessionGraph()
      ..edges = [edges[3]]
      ..nodes = [];
    await helper.checkRemove(sessionId, [], [edges[3].edgeId], expectedRemoved);

    // Node 4 is not connected to the graph. Removing it should not trigger any
    // other deletions.
    expectedRemoved = new SessionGraph()
      ..edges = []
      ..nodes = [nodes[4]];
    await helper.checkRemove(sessionId, [nodes[4].nodeId], [], expectedRemoved);

    // Removing node 1 should remove all connected edges, so all edges of the
    // graph but none of the other nodes.
    expectedRemoved = new SessionGraph()
      ..edges = edges
      ..nodes = [nodes[1]];
    await helper.checkRemove(sessionId, [nodes[1].nodeId], [], expectedRemoved);*/
  });

  test('Test observer', () async {
    final Map<String, LabelUri> labels = helper.labels;
    // Nodes and edges to be added.
    final List<NodeRecord> nodesToAdd =
        TestHelper.newList(3, TestHelper.newNode);
    final List<EdgeRecord> edgesToAdd = <EdgeRecord>[
      TestHelper.newEdge(
          nodesToAdd[0], [labels['A'], labels['B']], nodesToAdd[1]),
      TestHelper.newEdge(
          nodesToAdd[1], [labels['C'], labels['D']], nodesToAdd[2]),
    ];

    // Create an empty graph.
    final SessionId sessionId = await helper.checkCreateSession();
    final _TestObserver observer = new _TestObserver();
    // Add the observer. Then add some nodes and edges and receive them from the
    // observer.
    Completer completer = new Completer();
    helper.ledger.addObserver(sessionId, null, observer.stub,
        (final LedgerStatus status) => completer.complete());
    await completer.future;

    final Future<Null> change = observer.waitForChange();
    completer = new Completer();
    helper.ledger.updateSessionGraph(sessionId, nodesToAdd, edgesToAdd, null,
        null, null, (final LedgerStatus status) => completer.complete());
    await completer.future;
    await change;
    expect(observer.edgeCount, 2);
    expect(observer.nodeCount, 3);
    expect(observer.onChangeCount, 1);

    // Close the observer and stop receiving changes.
    final List<dynamic> extraNodes = TestHelper.newList(1, TestHelper.newNode);
    final List<EdgeRecord> extraEdges = <EdgeRecord>[
      TestHelper.newEdge(nodesToAdd[0], [labels['A']], extraNodes[0])
    ];
    observer.stub.close();
    completer = new Completer();
    helper.ledger.updateSessionGraph(sessionId, extraNodes, extraEdges, null,
        null, null, (final LedgerStatus status) => completer.complete());
    await completer.future;
    // No new chages should have been received.
    expect(observer.edgeCount, 2);
    expect(observer.nodeCount, 3);
  });
}

void main(List<String> args, Object handleToken) {
  runAppTests(handleToken, [ledgerTests, jsonQueryBuilderTests]);
}

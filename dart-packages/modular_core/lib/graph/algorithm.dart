// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'graph.dart';

typedef void PathVisitCallback(final Edge edge);

/// Traverses all paths in the graph from the given [Node]. Note that the graph
/// is a directed graph. This would call the callback only for the edges which
/// are reachable from the [Node] startnode.
void pathVisit(
    final Graph graph, final Node startNode, final PathVisitCallback f) {
  startNode.outEdges.forEach((final Edge outEdge) {
    _pathVisitEdge(outEdge, [], f);
  });
}

void _pathVisitEdge(Edge edge, final List<Edge> previouslyVisitedPath,
    final PathVisitCallback f) {
  if (previouslyVisitedPath.contains(edge)) {
    // We are in a loop. No need to match further.
    return;
  }
  final List<Edge> pathTillHere = <Edge>[]
    ..addAll(previouslyVisitedPath)
    ..add(edge);
  f(edge);
  edge.target.outEdges.forEach((final Edge outEdge) {
    _pathVisitEdge(outEdge, pathTillHere, f);
  });
}

// Returns a string representing all nodes and edges in the graph in a readable
// format(similar to flutter). This is useful for debugging.
// Since the node ids are long, this creates a map from node ids to local ids
// and prints the map at the start.
String printGraph(Graph graph, {bool shortenNodeIds: false}) {
  // Assumes edges are in order.
  Map<NodeId, String> simplifiedNodeIds = new Map<NodeId, String>();

  int count = 0;
  graph.nodes.forEach((final Node n) =>
      simplifiedNodeIds[n.id] = shortenNodeIds ? count++ : n.id);

  String accumulatedString = '';

  Set<Edge> printedEdges = new Set<Edge>();
  if (shortenNodeIds) {
    accumulatedString = '$accumulatedString NodeTable: $simplifiedNodeIds\n';
  }
  accumulatedString = '$accumulatedString Paths:\n';

  graph.nodes
      .where((final Node node) => node.inEdges.isEmpty)
      .forEach((final Node root) {
    root.outEdges.forEach((final Edge edge) {
      if (printedEdges.contains(edge)) {
        // This will not print properly, if the edges are not in order.
        return;
      }
      accumulatedString +=
          _printPath(edge, [], printedEdges, simplifiedNodeIds);
    });
  });

  // print the part of the graph which doesn't have any root.
  graph.edges.forEach((final Edge edge) {
    if (printedEdges.contains(edge)) {
      // This will not print properly, if the edges are not in order.
      return;
    }
    accumulatedString += _printPath(edge, [], printedEdges, simplifiedNodeIds);
  });
  return accumulatedString;
}

String _printPath(Edge edge, final List<Edge> previouslyVisitedPath,
    Set<Edge> printedEdges, Map<NodeId, String> simplifiedNodeIds,
    [String prefixLineOne = '', String prefixOtherLines = '']) {
  if (previouslyVisitedPath.contains(edge)) {
    // We are in a loop. No need to match further.
    return '';
  }
  final List<Edge> pathTillHere = <Edge>[]
    ..addAll(previouslyVisitedPath)
    ..add(edge);
  List<String> shorthandLabels = edge.labels.map((final String label) {
    Uri uri = Uri.parse(label);
    return uri.hasFragment ? uri.fragment : uri.pathSegments.last;
  }).toList();

  String result =
      '$prefixLineOne $shorthandLabels --> ${simplifiedNodeIds[edge.target.id]}\n';

  printedEdges.add(edge);
  if (edge.target.outEdges.isNotEmpty) {
    List<Edge> outEdges = edge.target.outEdges.toList();
    for (int i = 0; i < outEdges.length - 1; i++) {
      result += prefixOtherLines;
      result += _printPath(
          outEdges[i],
          pathTillHere,
          printedEdges,
          simplifiedNodeIds,
          "$prefixOtherLines  \u251C",
          "$prefixOtherLines \u2502");
    }
    result += prefixOtherLines;
    result += _printPath(outEdges.last, pathTillHere, printedEdges,
        simplifiedNodeIds, "$prefixOtherLines  \u2514", "$prefixOtherLines  ");
  }
  return result;
}

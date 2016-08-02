// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:parser/expression.dart' show Label;
import 'package:parser/manifest.dart' show Manifest;
import 'package:parser/recipe.dart' show Step;
import 'label_map.dart';

/// This class helps to format a recipe as a dot diagram. For every kind
/// of edge and node in the graph there is one method, so this class
/// captures the visual vocabulary of a recipe diagram. It also tracks
/// some of the content of the diagram, such as the different sets of
/// nodes and edges.
///
/// There are two kinds of nodes in the recipe diagram:
///
/// 1. A data node is a node in the data structure graph. Data nodes are
///    connected to each other.
///
/// 2. A step node is a node in the data flow graph. Step nodes are connected to
///    each other through data nodes: A data node is created as the output of
///    one step node, and consumed as input by another step node.
///
/// There are three kinds of edges in the recipe diagram:
///
/// 1. A data edge is part of the data structure graph and connects two data
///    nodes, where one data node contains the other.
///
/// 2. A type edge is part of the data structure graph and connects two data
///    nodes, where one data node is the more generalized (intrinsic) concept of
///    the other.
///
/// 3. A step edge is part of the data flow graph and connects a step to its
///    inputs, outputs, and scope.
///
class RecipeFormat {
  final Set<String> _composeNodes = new Set<String>();
  final Map<String, List<String>> _representations = <String, List<String>>{};
  final LabelMap _labelMap = new LabelMap();
  final StringBuffer _out = new StringBuffer();

  RecipeFormat() {
    _out.writeln('digraph {');
  }

  String finish() {
    for (final String label in _labelMap.labelId.keys) {
      _addDataNode(label);
    }
    _out.writeln('}');
    return _out.toString();
  }

  /// Adds a node to the diagram that represents a data label. It looks like a
  /// class in a UML class diagram. Data nodes are added implicitly to the
  /// diagram by mentioning them in a data edge and creating a label ID for
  /// them.
  void _addDataNode(final String label) {
    final String id = _labelMap.labelId[label];
    final String color = _composeNodes.contains(id) ? 'red' : 'orange';
    final List<String> representation = _representations[label] ?? <String>[];
    String labelText = label;
    for (final String type in representation) {
      labelText += '\n<$type>';
    }

    _out.writeln(
        '$id [color=$color,fontcolor=$color,label="$labelText",shape=box]');
  }

  /// Adds a node that represents a recipe step to the diagram. The node looks
  /// like an activity in a UML activity diagram. It returns the ID of the node
  /// so we can draw edges from it.
  String addStepNode(final Step step, final Manifest manifest) {
    final String stepNode = 'step${step.id}';
    String label = '${step.verb}';
    if (manifest?.title != null) {
      label += '\n[${manifest.title}]';
    }
    _out.writeln('$stepNode [shape=ellipse,label="$label"]');
    return stepNode;
  }

  /// Adds a representation type for a data item with a given semantic label.
  void addRepresentation(final Label property, final Label representation) {
    final String propertyKey = property.toString();
    final String representationKey = representation.toString();
    if (!_representations.containsKey(propertyKey)) {
      _representations[propertyKey] = [];
    }
    if (!_representations[propertyKey].contains(representationKey)) {
      _representations[propertyKey].add(representationKey);
    }
  }

  /// Adds an edge between two labels. This edge looks like aggregation in the
  /// UML class diagram. Each edge between two labels is only added once.
  void addDataEdge(final Label data0, final Label data1) {
    final String id0 = _labelMap.getLabelId(data0.toString());
    final String id1 = _labelMap.getLabelId(data1.toString());
    if (_labelMap.isNewEdge(id0, id1)) {
      _out.writeln('$id0 -> $id1 [color=orange,fontcolor=orange,' +
          'dir=both,arrowhead=normal,arrowtail=ediamond,style=solid]');
    }
  }

  /// Adds an edge between two labels. This edge is something like inheritance
  /// in the UML class diagram, but we don't know which colocated label
  /// represents the mode general concept, so there is no inheritance arrow.
  /// Each edge between two labels is only added once.
  void addTypeEdge(final Label data0, final Label data1) {
    final String id0 = _labelMap.getLabelId(data0.toString());
    final String id1 = _labelMap.getLabelId(data1.toString());
    // These edges are not directed, so we deduplicate in both directions.
    if (_labelMap.isNewEdge(id0, id1) && _labelMap.isNewEdge(id1, id0)) {
      _out.writeln('$id0 -> $id1 [color=orange,fontcolor=orange,' +
          'dir=both,arrowhead=odot,arrowtail=odot,style=solid]');
    }
  }

  /// Adds an edge from a node that represents a step to a node that represents
  /// data (i.e., a label in the session graph). This edge looks different
  /// depending on whether the data it leads to constitutes input, scope, or
  /// output of the step.
  void addStepEdge(final String stepNode, final Label data,
      {final bool output: false,
      final bool scope: false,
      final bool compose: false,
      final bool terminal: true}) {
    final String dataNode = _labelMap.getLabelId(data.toString());
    final String arrowhead = scope ? 'odot' : terminal ? 'normal' : 'empty';
    final String color =
        compose && terminal ? 'red' : terminal ? 'black' : 'grey';
    if (compose && terminal) {
      _composeNodes.add(dataNode);
    }
    // We only write the edge if none already exists. We check this in the
    // direction from the step node to the data node, even if we draw the edge
    // the other direction. Because scope and input are drawn first, this
    // suppresses components of the output path expressions that already appear
    // in the input, creating output edges only for the path expression
    // components that are actually created.
    if (_labelMap.isNewEdge(stepNode, dataNode)) {
      if (output) {
        _out.writeln(
            '$stepNode -> $dataNode [arrowhead=$arrowhead,color=$color,style=dashed]');
      } else {
        _out.writeln(
            '$dataNode -> $stepNode [arrowhead=$arrowhead,color=$color,style=solid]');
      }
    }
  }
}

// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:handler/module_instance.dart' show ModuleInstance;
import 'package:modular_core/graph/graph.dart' show Node, Edge, NodeId;
import 'package:parser/expression.dart' show Label, Verb;
import 'package:parser/recipe.dart' show Step;
import 'label_map.dart';

/// This class helps to format a session as a dot diagram. For every
/// kind of edge and node in the graph there is one method, so this
/// class captures the visual vocabulary of a session diagram.
///
/// There are two kinds of nodes in the session diagram:
///
/// 1. A data node is a node in the data structure graph. Data nodes are
///    connected to each other.
///
/// 2. A step node is a node in the data flow graph. Step nodes are
///    connected to each other through data nodes: A data node is
///    created as the output of one step node, and consumed as input by
///    another step node.
///
/// There are two kinds of edges in the session diagram:
///
/// 1. A data edge is part of the data structure graph and connects two data
///    nodes, where one data node contains the other.
///
/// 2. A step edge is part of the data flow graph and connects a step to
///    its inputs, outputs, and scope.
///
class SessionFormat {
  final LabelMap _labelMap = new LabelMap();
  final StringBuffer _out = new StringBuffer();

  final List<String> _stepNodes = <String>[];

  // Whether to also show data flow in the same diagram. If true,
  // renders the data structure graph in a separate color.
  final bool dataflow;

  // Map from full edge label to shorthand.
  final Map<String, String> shorthand;

  SessionFormat({this.dataflow: true, this.shorthand}) {
    _out.writeln('digraph {');
    //_out.writeln('rankdir = LR;');
  }

  String finish() {
    //_out.writeln('subgraph {');
    //_out.writeln(' rank = same;');
    _stepNodes.forEach((String s) => _out.writeln(s));
    //_out.writeln('}');
    _out.writeln('}');
    return _out.toString();
  }

  final Map<NodeId, int> _idMap = <NodeId, int>{};

  String _id(final Node node) {
    if (!_idMap.containsKey(node.id)) {
      _idMap[node.id] = _idMap.length;
    }
    return '${_idMap[node.id]}';
  }

  String _shorthand(final String label) {
    if (_shorthand != null && shorthand.containsKey(label)) {
      return shorthand[label];
    }
    return label;
  }

  /// Adds a data node to the diagram. The node ID is derived from the
  /// ID of the input node.
  void addDataNode(final Node node) {
    final String color = dataflow ? 'orange' : 'black';
    final String dataNode = 'data${_id(node)}';

    String label = _id(node);
    for (final String r in node.valueKeys) {
      label += '\n<${_shorthand(r)}>';
    }

    _out.writeln('$dataNode' +
        ' [color=$color,fontcolor=$color,shape=box,style=rounded,' +
        'label="$label"]');
  }

  /// Adds a step node to the diagram. The node ID is taken from the
  /// step ID in the parse tree. The step node is either drawn for a
  /// module instance if there is one, or otherwise as a "ghost"
  /// placeholder for future instances of the step.
  String addStepNode(final Step step, final ModuleInstance instance) {
    final String stepNode =
        instance == null ? 'verbproto${step.id}' : 'verb${instance.id}';
    final String style = instance == null ? 'dashed' : 'solid';
    final StringBuffer label = new StringBuffer();
    label.writeln('${_formatVerb(step.verb)}');
    if (instance?.manifest?.verb != null) {
      label.writeln('<${instance.manifest.url ?? "resolved"}>');
    }
    _stepNodes.add('$stepNode [shape=ellipse,style=$style,label="$label"]');
    return stepNode;
  }

  /// Adds a data edge to the diagram. The edge is drawn simpler for
  /// diagrams that show only the data structure of the graph and omit
  /// the data flow.
  void addDataEdge(final Edge edge) {
    final String color = dataflow ? 'orange' : 'black';
    final String arrowtail = dataflow ? 'odiamond' : 'none';
    final String startNode = 'data${_id(edge.origin)}';
    final String endNode = 'data${_id(edge.target)}';
    final List<String> labels =
        edge.labels.map((String label) => _shorthand(label)).toList();
    if (_labelMap.isNewEdge(startNode, endNode)) {
      _out.writeln('$startNode -> $endNode' +
          ' [color=$color,fontcolor=$color,style=solid,' +
          'dir=both,arrowhead=normal,arrowtail=$arrowtail,label="$labels"]');
    }
  }

  /// Adds a data flow edge to the diagram. The data flow edge indicates
  /// a relationship between a Node created by a module as output and
  /// the nodes in the input of this module.
  void addFlowEdge(final int inputId, final int outputId) {
    final String color = dataflow ? 'orange' : 'black';
    final String startNode = 'data$inputId';
    final String endNode = 'data$outputId';
    if (_labelMap.isNewEdge(startNode, endNode)) {
      _out.writeln('$startNode -> $endNode' +
          ' [color=$color,fontcolor=$color,style=dotted]');
    }
  }

  /// Adds a step edge to the diagram. The edge can be for an input,
  /// output, or scope of a step, and it can lead to data that already
  /// do or don't yet exist.
  void addStepEdge(
      final String stepNode, final Label label, final Node target,
      {final bool output: false, final bool terminal: true}) {
    final String arrow = terminal ? 'normal' : 'empty';

    if (target == null) {
      final String dataNode = _labelMap.getLabelId(label.toString());
      if (_labelMap.isNewEdge(stepNode, dataNode)) {
        _out.writeln('$dataNode [shape=box,style="rounded,dashed",label="*"]');
        _out.writeln(
            (output ? '$stepNode -> $dataNode' : '$dataNode -> $stepNode') +
                ' [label="$label",style=dashed,arrowhead=$arrow]');
      }
    } else {
      final String dataNode = 'data${_id(target)}';
      if (_labelMap.isNewEdge(stepNode, dataNode)) {
        final String style = output ? 'dashed' : 'solid';
        _out.writeln(
            (output ? '$stepNode -> $dataNode' : '$dataNode -> $stepNode') +
                ' [label="$label",style=$style,arrowhead=$arrow]');
      }
    }
  }

  static String _formatVerb(final Verb verb) {
    return verb == null ? '(null)' : verb.toString();
  }
}

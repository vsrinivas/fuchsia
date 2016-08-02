// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Functions to format some common data structures to text, for display
// in a terminal. The reason these are functions here and not methods on
// the respective classes are:
//
// * coherency of formatting across classes is more important than
//   encapsulation,
//
// * formatting is not essential to the classes, so we don't want to
//   clutter their definitions with it,
//
// * formatting doesn't need to expose information only internally
//   known.

import 'package:modular_core/graph/graph.dart';
import 'package:handler/module.dart';
import 'package:handler/module_instance.dart';
import 'package:handler/session.dart';
import 'package:handler/session_match.dart';
import 'package:handler/session_pattern.dart';
import 'package:parser/expression.dart';
import 'package:parser/recipe.dart';

import 'recipe_format.dart';
import 'session_format.dart';
import 'simulation_module_output.dart';
import 'simulation_module_runner.dart';

class Out {
  // Accumulates output here.
  final StringBuffer _buffer;

  // Indentation level.
  final int _level;

  // When creating item(), tracks first line.
  bool _first;

  Out()
      : _buffer = new StringBuffer(),
        _level = 0,
        _first = false;

  Out.sub(this._buffer, this._level, this._first);

  void field(final String label, final String value) {
    startline();
    text(label);
    text(': ');
    text(value);
    endline();
  }

  void value(final String value) {
    final Out sub = item();
    sub.startline();
    sub.text(value);
    sub.endline();
  }

  void startline() {
    if (_first) {
      _buffer.write('  ' * (_level - 1));
      _buffer.write('- ');
    } else {
      _buffer.write('  ' * _level);
    }
  }

  void endline() {
    _buffer.write('\n');
    _first = false;
  }

  void text(final String t) {
    _buffer.write(t);
  }

  Out section(final String label) {
    startline();
    text(label);
    text(':');
    endline();
    return new Out.sub(_buffer, _level + 1, false);
  }

  Out item() {
    return new Out.sub(_buffer, _level, true);
  }

  void list(final String label, final List<dynamic> data, final String sep,
      final Function access) {
    field(label, _formatList(data, sep, access));
  }

  void map(final String sec, final Map<String, String> map) {
    if (map.isNotEmpty) {
      final Out sectionOut = section(sec);
      map.forEach((String key, String value) {
        sectionOut.item().field(key, value);
      });
    }
  }

  @override
  String toString() => _buffer.toString();
}

String _formatVerb(final Verb verb) {
  return verb == null ? '(null)' : verb.toString();
}

void _section(final Out out, final String name, final dynamic value) {
  if (value == null) {
    return;
  }

  final Out sectionOut = out.section(name);
  if (value is List<dynamic>) {
    for (final dynamic item in value) {
      sectionOut.value(item.toString());
    }
  } else {
    sectionOut.value(value.toString());
  }
}

String formatRecipe(final Recipe recipe) {
  final Out out = new Out();
  out.field('title', recipe.title);
  out.field('verb', _formatVerb(recipe.verb));

  _section(out, 'input', recipe.input);
  _section(out, 'output', recipe.output);

  final Out recipeOut = out.section('recipe');
  for (final Step step in recipe.steps) {
    final Out stepOut = recipeOut.item();
    stepOut.field('verb', _formatVerb(step.verb));

    _section(stepOut, 'scope', step.scope);
    _section(stepOut, 'input', step.input);
    _section(stepOut, 'output', step.output);
    _section(stepOut, 'display', step.display);
  }

  _section(
      out,
      'use',
      recipe.use.shorthand.values
          .map((final Label label) => '${label.shorthand}: ${label.uri}')
          .toList());

  return out.toString();
}

String formatUse(final Recipe recipe) {
  final Out out = new Out();
  out.map(
      'shorthand',
      new Map<String, String>.fromIterables(recipe.use.shorthand.keys,
          recipe.use.shorthand.values.map((Label l) => l.toString())));
  return out.toString();
}

String formatGraph(final Graph graph) {
  return graph.toString();
}

String _formatList(final List<dynamic> list, final String separator,
    final Function formatItem) {
  if (list == null) {
    return '';
  }
  return list.map(formatItem).join(separator);
}

String _formatInstance(final ModuleInstance instance) {
  return '[${instance.id}] ${_formatVerb(instance.step.verb)}';
}

String formatOutputs(final Session session) {
  final Out out = new Out();
  final Out outputSection = out.section('outputs');
  for (final Module module in session.runner.modules) {
    for (final ModuleInstance instance in module.activeInstances) {
      for (final SimulationModuleOutput output in _outputs(instance)) {
        outputSection.item().value('${_formatInstance(instance)} ::' +
            ' [${output.id}] ${output.expr}' +
            ' ' +
            ('*' * output.emittedValues.length));
      }
    }
  }
  return out.toString();
}

void _formatSessionPattern(final Out out, final SessionPattern io) {
  int complete = 0;
  int partial = 0;
  io.matches.forEach((final SessionMatch m) {
    if (m.isComplete) {
      ++complete;
    } else {
      ++partial;
    }
  });
  out.field('${io.pathExpr}',
      '${io.matches.length} matches, $complete complete, $partial partial');
}

String formatSession(final Session session) {
  final Out out = new Out();
  final Out moduleSection = out.section('module');
  for (final Module module in session.runner.modules) {
    final Out moduleItem = moduleSection.item();
    moduleItem.field(_formatVerb(module.step.verb), '${module.manifest?.url}');

    if (module.scope != null) {
      _formatSessionPattern(moduleItem.section('scope').item(), module.scope);
    }

    if (module.inputs.isNotEmpty) {
      final Out inputSection = moduleItem.section('inputs');
      for (final SessionPattern input in module.inputs) {
        _formatSessionPattern(inputSection.item(), input);
      }
    }

    if (module.outputs.isNotEmpty) {
      final Out outputSection = moduleItem.section('outputs');
      for (final SessionPattern output in module.outputs) {
        _formatSessionPattern(outputSection.item(), output);
      }
    }

    int instances = 0;
    module.activeInstances.forEach((_) => ++instances);
    if (instances > 0) {
      final Out instanceSection = moduleSection.section('instances');
      for (final ModuleInstance instance in module.activeInstances) {
        final Out instanceItem = instanceSection.item();
        final Step step = instance.step;
        if (step.input != null && step.input.isNotEmpty) {
          final Out inputSection = instanceItem.section('input');
          for (final SessionMatch match in instance.inputMatches) {
            inputSection.item()
              ..field('${match.pathExpr}', '${match.scope.length} nodes');
          }
        }
        if (step.output != null && step.output.isNotEmpty) {
          final Out outputSection = instanceItem.section('output');
          for (final SimulationModuleOutput output in _outputs(instance)) {
            outputSection.item()..value('[${output.id}] ${output.expr}');
          }
        }
      }
    }
  }

  return out.toString();
}

/// Adds all edges to represent a path expression in scope, input, or
/// output expressions in the recipe.
void _addPath(
    final RecipeFormat out, final String stepNode, final PathExpr expr,
    {final bool output: false,
    final bool scope: false,
    final bool compose: false}) {
  Property expr0 = expr.properties[0];
  for (int i = 1; i < expr.properties.length; ++i) {
    final Property expr1 = expr.properties[i];
    out.addDataEdge(expr0.labels.first, expr1.labels.first);
    expr0 = expr1;
  }

  final int count = expr.properties.length;
  for (int i = 0; i < count; ++i) {
    final Property property = expr.properties[i];

    for (final Label r in property.representations) {
      out.addRepresentation(property.labels.first, r);
    }

    final bool terminal = i == count - 1;
    out.addStepEdge(stepNode, property.labels.first,
        output: output, scope: scope, compose: compose, terminal: terminal);

    Label label0;
    for (final Label label1 in property.labels) {
      if (label0 != null) {
        out.addTypeEdge(label0, label1);
      }
      label0 = label1;
    }
  }
}

// Formats one recipe as a dot diagram.
String formatRecipeDot(final Session session) {
  final RecipeFormat out = new RecipeFormat();

  for (final Module module in session.runner.modules) {
    final Step step = module.step;
    final String stepNode = out.addStepNode(step, module.manifest);

    if (step.scope != null) {
      _addPath(out, stepNode, step.scope, scope: true);
    }

    if (step.input != null) {
      for (final PathExpr expr in step.input) {
        _addPath(out, stepNode, expr);
      }
    }

    if (step.output != null) {
      for (final PathExpr expr in step.output) {
        _addPath(out, stepNode, expr, output: true);
      }
    }

    if (step.display != null) {
      for (final PathExpr expr in step.display) {
        _addPath(out, stepNode, expr, output: true, compose: true);
      }
    }

    if (step.compose != null) {
      for (final PathExpr expr in step.compose) {
        _addPath(out, stepNode, expr, compose: true);
      }
    }
  }

  return out.finish();
}

Map<String, String> _makeShorthandMap(final Uses use) {
  final Map<String, String> ret = <String, String>{};
  use.shorthand.forEach((final String key, final Label value) {
    ret[value.uri.toString()] = key;
  });
  return ret;
}

// Formats a session as a dot diagram. The diagram shows both the data
// structure and the data flow in the session.
String formatSessionDot(final Session session, {final bool meta: false}) {
  final SessionFormat out =
      new SessionFormat(shorthand: _makeShorthandMap(session.recipe.use));

  // The data structure of the session shown as nodes connected by edges
  // from the session graph.
  for (final Node node in session.graph.nodes) {
    if (meta ||
        !node.inEdges.any((Edge e) =>
            e.labels.any((String l) => l.startsWith('internal:')))) {
      out.addDataNode(node);
    }
  }
  for (final Edge edge in session.graph.edges) {
    if (meta || !edge.labels.any((String l) => l.startsWith('internal:'))) {
      out.addDataEdge(edge);
    }
  }

  // The data flow of the session is between steps in the recipe. We
  // draw both data flow connections already established between
  // instantiated modules in the session, as well as potential data flow
  // connections between steps in the session that were not yet
  // instantiated.
  for (final Module module in session.runner.modules) {
    // First, draw step nodes for existing module instances. These step
    // nodes have inputs that actually exist (i.e., are covered by
    // matches). Their outputs may or may not yet exist.
    for (final ModuleInstance instance in module.activeInstances) {
      final String stepNode = out.addStepNode(module.step, instance);

      for (final SessionMatch match in instance.inputMatches) {
        final List<Node> target1 = match.targetList(match.length - 1);
        for (final Node target in target1) {
          out.addStepEdge(
              stepNode, match.pathExpr.properties.last.labels.first, target);
        }
      }

      for (final SimulationModuleOutput output in _outputs(instance)) {
        final int count = output.expr.properties.length;
        for (final SimulationOutputValue value in output.emittedValues) {
          for (int i = 0; i < count; ++i) {
            final bool terminal = i == count - 1;
            for (final Edge target in value.targets[i]) {
              if (target != null && terminal) {
                out.addStepEdge(stepNode, value.expr.properties[i].labels.first,
                    target.target,
                    output: true, terminal: terminal);
              }
            }
          }
        }

        if (output.emittedValues.isEmpty) {
          for (int i = 0; i < count; ++i) {
            final bool terminal = i == count - 1;
            if (terminal) {
              out.addStepEdge(
                  stepNode, output.expr.properties[i].labels.first, null,
                  output: true, terminal: terminal);
            }
          }
        }
      }
    }

    // For a module that has no instances, draw placeholder step nodes.
    // These step nodes may have some inputs that already exist, and
    // some inputs that don't yet exist (which is why there are no
    // instances yet). Existing matches may be both complete and
    // partial, and we draw both. For such step nodes, naturally all
    // outputs don't yet exist.
    if (module.instances.isEmpty) {
      final String stepNode = out.addStepNode(module.step, null);

      if (module.inputs != null) {
        for (final SessionPattern input in module.inputs) {
          // We draw edges to all nodes matched by inputs, both partial
          // and complete. We keep track of which parts of the input
          // expressions are matched, so that we can draw the unmatched
          // parts too (below).
          final List<bool> targetMap = <bool>[];
          final int count = input.pathExpr.properties.length;
          targetMap.length = count;

          for (final SessionMatch match in input.matches) {
            for (int i = 0; i < count; ++i) {
              final bool terminal = i == count - 1;
              for (final Node target in match.targetList(i)) {
                targetMap[i] = true;
                if (terminal) {
                  out.addStepEdge(stepNode,
                      match.pathExpr.properties[i].labels.first, target,
                      terminal: terminal);
                }
              }
            }
          }

          // Draw an empty input for unmatched parts of input expressions.
          for (int i = 0; i < count; ++i) {
            final bool terminal = i == count - 1;
            if (!targetMap[i] && terminal) {
              out.addStepEdge(
                  stepNode, input.pathExpr.properties[i].labels.first, null,
                  terminal: terminal);
            }
          }
        }
      }

      // Draw empty outputs.
      if (module.step.output != null) {
        for (final PathExpr output in module.step.output) {
          final int count = output.properties.length;
          for (int i = 0; i < count; ++i) {
            final bool terminal = i == count - 1;
            if (terminal) {
              out.addStepEdge(stepNode, output.properties[i].labels.first, null,
                  output: true, terminal: terminal);
            }
          }
        }
      }
    }
  }

  return out.finish();
}

// Formats the data structure of a session as a dot diagram.
String formatGraphDot(final Session session, {final bool meta: false}) {
  final Graph graph = session.graph;

  final SessionFormat out = new SessionFormat(
      dataflow: false, shorthand: _makeShorthandMap(session.recipe.use));

  for (final Node node in graph.nodes) {
    if (meta ||
        !node.inEdges.any((Edge e) =>
            e.labels.any((String l) => l.startsWith('internal:')))) {
      out.addDataNode(node);
    }
  }

  for (final Edge edge in graph.edges) {
    if (meta || !edge.labels.any((String l) => l.startsWith('internal:'))) {
      out.addDataEdge(edge);
    }
  }

  return out.finish();
}

Iterable<SimulationModuleOutput> _outputs(final ModuleInstance instance) {
  return (instance.runner as SimulationModuleRunner).outputs;
}

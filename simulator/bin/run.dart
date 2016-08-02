// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// The session runner evaluates a recipe together with modules provided
/// on the command line in order to produce and modify a session graph.
///
/// What happens during a session run:
///
/// 1. All modules that have all inputs present in the session are
///    instantiated either once or once for every match, according to
///    cardinality qualifiers. Specifically, at the start when the
///    session graph is empty, all modules are instantiated that have no
///    required inputs.
///
/// 2. Of all instantiated modules, outputs are displayed.
///
/// 3. From any of the open outputs a value can be emitted, thus adding
///    to the the session graph and possibly causing new modules to be
///    instantiated according to 1.
///
/// The main loop reads a command from the prompt. Commands available
/// are listed in the output of the help command. The commands allow to
/// inspect and modify the session.
///
/// Specifically, some commands allow to visualize the session as a
/// graph using xdot. This requires xdot to be installed.

import 'dart:async';
import 'dart:io';
import 'package:args/args.dart';
import 'package:handler/graph/session_graph.dart';
import 'package:handler/handler.dart';
import 'package:handler/session.dart';
import 'package:parser/expression.dart';
import 'package:parser/expression_parser.dart';
import 'package:parser/manifest.dart';
import 'package:parser/parse_error.dart';
import 'package:parser/parser.dart';
import 'package:parser/recipe.dart';

import '../lib/format.dart';
import '../lib/simulation_module_runner.dart';

void usage(final ArgParser parser) {
  print('');
  print('usage: run [options] <recipe>');
  print('');
  print('options:');
  print('');
  print(parser.usage);
  print('');
}

// Returns Future<int> or Future<File>
Future<dynamic> _dot(final String graph, final List<String> token) async {
  if (token.length == 1) {
    Process dot = await Process.start('xdot', []);
    dot.stdin.write(graph);
    dot.stdin.close();
    return dot.exitCode;
  }

  if (token[1] == '-') {
    print(graph);
    return new Future<int>.value(0);
  }

  return new File(token[1]).writeAsString(graph);
}

class State {
  final List<Manifest> manifests;
  final Recipe recipe;
  final Session session;
  final SimulationModuleRunnerFactory simulationModuleRunnerFactory;
  final Graph graph;
  final Handler handler;

  State._internal(this.manifests, this.recipe, this.session,
      this.simulationModuleRunnerFactory, this.graph, this.handler);

  static Future<State> newState(
      final List<Manifest> manifests, final Recipe recipe) async {
    final SimulationModuleRunnerFactory simulationModuleRunnerFactory =
        new SimulationModuleRunnerFactory();
    final Handler handler = new Handler(
        manifests: manifests, runnerFactory: simulationModuleRunnerFactory);
    final Session session = await handler.createSession(recipe);
    final SessionGraph graph = session.graph;
    return new State._internal(manifests, recipe, session,
        simulationModuleRunnerFactory, graph, handler);
  }

  String get prompt {
    return '${session.recipe.verb} ${session.recipe.steps.length} steps';
  }
}

Future<Null> main(final List<String> args) async {
  final ArgParser parser = new ArgParser();
  parser.addOption('input',
      allowMultiple: true, help: 'Initial value in the session graph.');

  parser.addOption('module',
      allowMultiple: true,
      help: 'A module manifest file to register with the resolver.');

  parser.addOption('exec',
      allowMultiple: true, help: 'Command to execute at startup.');

  final ArgResults results = parser.parse(args);

  if (results.rest.length != 1) {
    usage(parser);
    exit(0);
  }

  final List<Manifest> manifests = <Manifest>[];
  for (final String manifest in results['module']) {
    manifests.addAll(parseManifests(await new File(manifest).readAsString())
      ..forEach((final Manifest m) {
        m.src = manifest;
      }));
  }

  final Recipe recipe = new Recipe.parseYamlString(
      await new File(results.rest[0]).readAsString());

  // Synthesize manifests for all recipe steps so that every step can be
  // resolved. If a step is not resolved, the Handler doesn't create a module
  // runner for it. This breaks the output simulation, because it's the
  // simulation module runner that actually inserts the dummy output values into
  // the graph.
  for (final Step step in recipe.steps) {
    manifests.add(new Manifest.fromStep(step));
  }

  final State state = await State.newState(manifests, recipe);

  for (final String input in results['input']) {
    final ParserState parserState = new ParserState();
    parserState.shorthand.addAll(state.recipe.use.shorthand);
    final Scanner scanner =
        new Scanner(parserState, new SourceLocation.inline(), input);
    final Property property = parseProperty(scanner);

    try {
      scanner.checkDone();
    } on ParseError {
      print('parse error in input label $input, ignoring');
      print(scanner.debug);
      continue;
    }

    state.graph.mutate((GraphMutator mutator) {
      mutator.addEdge(state.session.root.id,
          property.labels.map((final Label label) => label.toString()));
    });
  }

  state.session.start();

  if (results['exec'].length > 0) {
    for (final String input in results['exec']) {
      await processInput(input, state);
    }
  } else {
    while (true) {
      // Print a prompt.
      stdout.write('${state.prompt} > ');

      final String input = stdin.readLineSync();
      if (input == null) {
        // STDIN was closed.
        stdout.write('\n');
        return;
      }
      await processInput(input, state);
    }
  }
}

Future<Null> processInput(final String input, final State state) async {
  final List<String> token = input.trim().split(new RegExp('\\s+'));

  switch (token[0]) {
    case '':
      break;

    case 'help':
      print('commands: help, recipe, recipedot [filename], use,' +
          ' graph, graphdot [filename], edge, session, sessiondot [filename],' +
          ' sessionmetadot [filename],' +
          ' outputs,' +
          ' output, output-instance, output-all');
      break;

    case 'recipe':
      print(formatRecipe(state.recipe));
      break;

    case 'recipedot':
      await _dot(formatRecipeDot(state.session), token);
      break;

    case 'use':
      print(formatUse(state.recipe));
      break;

    case 'graph':
      print(formatGraph(state.graph));
      break;

    case 'graphdot':
      await _dot(formatGraphDot(state.session), token);
      break;

    case 'edge':
      if (token.length != 3) {
        print('usage: edge <start> <label>');
        return;
      }
      final Node start = state.graph.node(new NodeId.fromString(token[1]));
      if (start == null) {
        print('no node in graph ${token[1]}');
        return;
      }
      final Label label = state.recipe.use.shorthand[token[2]];
      if (label == null) {
        print('no edge label in recipe ${token[2]}');
        return;
      }
      state.graph.mutate((GraphMutator mutator) {
        final Edge newEdge = mutator.addEdge(start.id, [label.toString()]);
        print('added node ${newEdge.target.id}');
      });

      break;

    case 'session':
      print(formatSession(state.session));
      break;

    case 'sessiondot':
      await _dot(formatSessionDot(state.session), token);
      break;

    case 'sessionmetadot':
      await _dot(formatSessionDot(state.session, meta: true), token);
      break;

    case 'outputs':
      print(formatOutputs(state.session));
      break;

    case 'output':
      if (token.length != 2) {
        print('usage: output <id>; id is shown in outputs.');
        return;
      }
      final int id = int.parse(token[1]);
      state.simulationModuleRunnerFactory.triggerOutput(id);
      print(formatOutputs(state.session));
      break;

    case 'output-instance':
      if (token.length != 2) {
        print('usage: output-instance <id>; id is shown in instances.');
        return;
      }
      final int id = int.parse(token[1]);
      state.simulationModuleRunnerFactory.triggerOutputInstance(id);
      print(formatOutputs(state.session));
      break;

    case 'output-all':
      state.simulationModuleRunnerFactory.triggerOutputAll();
      print(formatOutputs(state.session));
      break;

    default:
      print('command not recognized: $token');
      break;
  }
}

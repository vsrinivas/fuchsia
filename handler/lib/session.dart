// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// Classes that hold the state of execution of a recipe, aka session.
///
/// The class hierarchy here corresponds to the class hierarchy that
/// represents a Recipe, and instances of the classes here hold
/// references to instances of their counterparts in the Recipe
/// hierarchy (arrows are references):
///
///
///   Handler
///     |
///     |
///     v
///   Session ---------> RecipeRunner ----> Recipe
///     |                  |                  |
///     |                  |                  |
///     |                  |                  |
///     |                  v                  v
///     |    +---------- Module ----------> Step
///     |    |             |                  |
///     |    |             |                  |
///     |    |             |                  |
///     |    |             v                  |
///     |    |           SessionPattern ------+
///     |    |             |                  |
///     |    |             |                  |
///     |    |             |                  |
///     v    v             v                  v
///   ModuleInstance --> SessionMatch ----> PathExpr
///
///

import 'dart:async';
import 'dart:developer' show Timeline;

import 'package:modular_core/uuid.dart';
import 'package:modular_core/graph/graph.dart';
import 'package:modular_core/log.dart';
import 'package:modular_core/util/timeline_helper.dart';
import 'package:parser/manifest.dart';
import 'package:parser/recipe.dart';

import 'bindings.dart';
import 'constants.dart';
import 'graph/impl/lazy_cloned_session_graph.dart';
import 'graph/session_graph.dart';
import 'handler.dart';
import 'inspector_json_server.dart';
import 'module.dart';
import 'module_instance.dart';
import 'module_runner.dart';
import 'recipe_runner.dart';
import 'session_match.dart';
import 'session_metadata.dart';

typedef SessionStartedCallback(final Session session);
typedef SessionStoppedCallback(final Session session);

/// Holds all the data of a session together, most important the module
/// configuration (expressed as a recipe) and the session graph. Created and
/// operated on by the Handler, cf. handler.dart. The Session delegates the
/// decision to create module instances to the RecipeRunner.
class Session implements Inspectable {
  static Logger _log = log('handler.Session');

  final Uuid id;

  /// The context the module instances execute in.
  final SessionGraph graph;

  /// The reference to the handler object, mainly to obtain the module index for
  /// now. But handler would be used to create sub-sessions from the current
  /// session eventually.
  final Handler handler;

  /// Metadata of the session read from and written to the graph.
  final SessionMetadata _metadata;

  /// Running a recipe is one way of creating module instances in the session.
  RecipeRunner _runner;

  /// Whether the session has been started.
  bool _started = false;
  bool get isStarted => _started;

  /// Indicates whether this session can be restarted after a call to [stop()].
  /// For example, a session that is backed by a [LazyClonedSessionGraph] cannot
  /// be restarted.
  bool _canRestart = true;

  /// Indicates whether this session is temporary. A temporary session is not
  /// written to the ledger and cannot be restarted once it was stopped.
  bool get isTemporary => graph is LazyClonedSessionGraph;

  /// The module instances running in this session.
  final List<ModuleInstance> _moduleInstances = <ModuleInstance>[];

  /// Given the URL of a module implementation (as obtained from a manifest),
  /// can create a running module instance.
  final ModuleRunnerFactory _runnerFactory;

  /// Callbacks used to notify an observer when this session gets started
  /// and stopped.
  final SessionStartedCallback _startedCallback;
  final SessionStoppedCallback _stoppedCallback;

  final InspectorJSONServer _inspector;

  /// HACK(mesch): Reentrant calls happen when update() causes a module instance
  /// to be created or updated, and its module graph updates the bindings. We
  /// suppress such reentrant updates. TODO(mesch): Such updates should really
  /// not happen at all. The Session should push updates synchronously through
  /// the system, and know which updates need to be propagated and which not.
  bool _updating = false;

  factory Session.fromGraph(
      {final Uuid id,
      final SessionGraph graph,
      final Handler handler,
      final ModuleRunnerFactory runnerFactory,
      final SessionStartedCallback startedCallback,
      final SessionStoppedCallback stoppedCallback,
      final InspectorJSONServer inspector}) {
    return traceSync(
        'new Session.fromGraph',
        () => new Session._internal(id, graph, handler, runnerFactory,
            startedCallback, stoppedCallback, inspector));
  }

  factory Session.fromRecipe(
      {final Uuid id,
      final Recipe recipe,
      final SessionGraph graph,
      final Handler handler,
      final ModuleRunnerFactory runnerFactory,
      final SessionStartedCallback startedCallback,
      final SessionStoppedCallback stoppedCallback,
      final InspectorJSONServer inspector}) {
    return traceSync('new Session.fromRecipe', () {
      return new Session._internal(id, graph, handler, runnerFactory,
          startedCallback, stoppedCallback, inspector, recipe);
    });
  }

  Session._internal(
      this.id,
      final SessionGraph graph_,
      this.handler,
      this._runnerFactory,
      this._startedCallback,
      this._stoppedCallback,
      this._inspector,
      [final Recipe recipe])
      : _metadata = new SessionMetadata(graph_, graph_.metadataNode),
        graph = graph_ {
    if (recipe != null) {
      _metadata.setRecipe(recipe);
    }
    _runner = new RecipeRunner(this, _inspector);

    _inspector?.publish(this);
    _inspector?.publishGraph(this.graph, inspectorPath + '/graph');
  }

  @override // Inspectable
  String get inspectorPath => '/session/$id';

  @override // Inspectable
  Future<dynamic> inspectorJSON() async {
    return {
      'type': 'session',
      'id': id.toString(),
      'graph': _inspector?.graph(graph),
      'rootNode': graph.root,
      'metadataNode': graph.metadataNode,
      'modules':
          await Future.wait(modules.map((Module m) => m.inspectorJSON())),
    };
  }

  @override // Inspectable
  Future<dynamic> onInspectorPost(dynamic json) async {}

  SessionMetadata get metadata => _metadata;

  // TODO(mesch): Only for simulator.
  Recipe get recipe => _metadata.getRecipe();

  // TODO(mesch): Only for tests.
  List<Module> get modules => _runner.modules;

  /// The root node of the session in [graph].
  Node get root => graph.root;

  RecipeRunner get runner => _runner;

  /// Starts a session, which starts running the recipe and creating modules
  /// there by.
  void start() {
    return Timeline.timeSync("$runtimeType start", () {
      assert(!_started);
      if (!_canRestart) {
        _log.warning('Session cannot be restarted: $id');
        return;
      }

      _started = true;

      // We cannot start a session without the runner object.
      assert(_runner != null);

      if (id != null) {
        print("-" * 80);
        print("  Handling session: ${id.toBase64()}");
        print("  From recipe: $recipe");
        print("-" * 80);
      }

      graph.addObserver(_onGraphChange);

      // Also calls _onGraphChange().
      update(addSteps: _metadata.getRecipe().steps);

      _inspector?.notify(this);

      if (_startedCallback != null) _startedCallback(this);
    });
  }

  /// Stops the running session, which also stop session state to listen for
  /// graph changes.
  /// The session object cleaned up, meaning after the stop it can be reused by
  /// calling start again.
  void stop() {
    assert(_started);
    _started = false;

    if (isTemporary) _canRestart = false;

    graph.removeObserver(_onGraphChange);

    _runner.clear();

    _inspector?.notify(this);

    if (_stoppedCallback != null) _stoppedCallback(this);
  }

  /// Called from the module runner of a module instance that received output
  /// from its implementation. This mutation is not yet applied to the graph,
  /// and it is not a pure metadata mutation.
  ///
  /// This applies the mutations to the graph, which triggers the
  /// _onGraphChange() callback, which in turn propagates the changes to the
  /// recipe runner and all module instances.
  void updateOutput(
      final ModuleInstance instance, final List<GraphMutation> mutations) {
    graph.mutate((final GraphMutator mutator) {
      mutations.forEach(mutator.apply);
    });
  }

  /// Called whenever the graph changes. There are three sources for this:
  ///
  /// 1. The ledger sends an update. This update needs to be applied to all
  ///    module instances.
  ///
  /// 2. A module instance sends an update. This update needs to be propagated
  ///    to the ledger, and to all affected module instances.
  ///
  /// 3. Suggestinator updates the recipe or the graph. The recipe runner is
  ///    reconfigured, and new module instances created accordingly.
  ///
  /// Eventually, all three operations should be triggered by separate methods,
  /// so we don't have to secondguess it here.
  void _onGraphChange(final GraphEvent event) {
    Timeline.timeSync("$runtimeType _onGraphChange", () {
      final GraphMutation mutation = event.mutations.firstWhere(
          (final GraphMutation mutation) =>
              mutation.type == GraphMutationType.setValue &&
              mutation.valueKey == Constants.recipeLabel,
          orElse: () => null);
      if (mutation != null) {
        _handleRecipeChange();
      }

      // HACK(mesch): If this update comes from writing only Bindings while an
      // update is in progress, we must not process it at all, lest we run in an
      // infinite loop. This happens for bindings written during an input
      // update. Bindings of outputs are written during updateOutput() and they
      // are let through.
      if (_updating &&
          event.mutations.withTag(Binding.mutationTag).isNotEmpty) {
        return;
      }

      _updating = true;

      // Updates the matches by the recipe runner, which determines which module
      // instances exist. Creates new module instances accodingly.
      final GraphMutationList mutations =
          event.mutations.coalesced.withoutTag(Binding.mutationTag);
      final SessionMatchUpdate updateData =
          new SessionMatchUpdate.fromGraphMutations(event.graph, mutations);
      _runner.updateMatches(updateData);

      // This determines whether the anchor set of the inputs of the module
      // instances has changed, and if it has, updates the anchor set. Then
      // updates all manifest matches in the module instance inputs, and
      // notifies the module runners of the modules with changed inputs.
      for (final ModuleInstance moduleInstance in _moduleInstances) {
        moduleInstance.updateMatches(updateData);
      }

      // Persists the changed bindings of the module instances. This is done
      // after updateMatches() because this may delete edges which are contained
      // in updateData.edges, so we must do this only after the edges are no
      // longer used.
      //
      // TODO(mesch): Cleaner would be to only send changed edges and their
      // closure to updateMatches(), *and* filter out changed metadata edges. We
      // could also filter out metadata edges altogether from the current edge
      // list.
      //
      // This changes the graph and thus calls the current function again. We
      // prevent gratuitous updates using the _updating flag.
      for (final ModuleInstance moduleInstance in _moduleInstances) {
        moduleInstance.updateBinding();
      }

      // Removes empty matches and resets the dirty flags.
      _runner.clearMatches();
      for (final ModuleInstance moduleInstance in _moduleInstances) {
        moduleInstance.clearMatches();
      }

      _updating = false;
    });

    _inspector?.notify(this);
  }

  /// Updates the session recipe by adding new steps from addSteps and removing
  /// steps from removeSteps. Module instances for new steps are started and
  /// module instances corresponding to the removeSteps will be destroyed.
  void update(
      {final Iterable<Step> addSteps, final Iterable<Step> removeSteps}) {
    assert(_started);
    assert(addSteps != null || removeSteps != null);

    final List<Step> currentSteps = new List<Step>.from(_runner.steps);
    final List<Step> targetSteps = new List<Step>.from(currentSteps);

    if (removeSteps != null && removeSteps.isNotEmpty) {
      removeSteps.forEach((final Step step) {
        assert(currentSteps.contains(step));
        targetSteps.remove(step);
      });
    }
    // Check the recipe and make sure we're not duplicating any existing
    // steps.
    final List<Step> actuallyAddedSteps = <Step>[];
    addSteps?.forEach((final Step step) {
      // We don't want to add the same step twice.
      if (targetSteps.contains(step)) {
        return; // continue;
      }

      actuallyAddedSteps.add(step);
      targetSteps.add(step);
    });

    // Create modules for new steps and remove modules for removed steps.
    removeSteps?.forEach((final Step step) => _runner.removeStep(step));
    actuallyAddedSteps.forEach(_addStep);

    // Save the recipe.
    //
    // NOTE(mesch): This will NOT trigger an update of the recipe runner IF this
    // invocation was from _handleRecipeChange(), because in that case the
    // recipe stored in the graph is the same as the current recipe, and a
    // setValue() to the same value suppresses the change notification.
    // Therefore, the RecipeRunner observer must be registered AFTER the Session
    // observer, so that the primary recipe update event first causes the
    // Session to update the recipe runner configuration, and then the recipe
    // runner to update the session matches. (This cost me a day.)
    final Recipe recipe = _metadata.getRecipe();
    recipe.steps.clear();
    recipe.steps.addAll(_runner.steps);
    _metadata.setRecipe(recipe);

    // Triggers matching of all step inputs and outputs and corresponding
    // instantiation of modules after changes made to the recipe above.
    _onGraphChange(new GraphEvent(graph, []));

    _inspector?.notify(this);
  }

  /// Reloads all those steps which are resolved to the manifest with
  /// urlPattern. The new step will use reload Url to load the module.
  void reloadStepsWithUrlPattern(final String urlPattern, final Uri reloadUrl) {
    List<Step> addSteps = <Step>[];
    List<Step> removeSteps = <Step>[];
    final RegExp regex = new RegExp(urlPattern);
    _runner.modules.where((final Module m) {
      return regex.hasMatch(m.manifest?.url?.toString());
    }).forEach((final Module m) {
      final Step newStep = new Step(m.step.scope, m.step.verb, m.step.input,
          m.step.output, m.step.display, m.step.compose, reloadUrl);
      removeSteps.add(m.step);
      addSteps.add(newStep);
    });

    if (addSteps.isNotEmpty || removeSteps.isNotEmpty) {
      update(addSteps: addSteps, removeSteps: removeSteps);
    }
  }

  @override
  String toString() => _runner.modules.toString();

  // Recipe has changed. Hence we should update the recipe runner.
  void _handleRecipeChange() {
    final Recipe newRecipe = _metadata.getRecipe();
    final Set<Step> newSteps = new Set<Step>.from(newRecipe.steps);
    final Set<Step> oldSteps = new Set<Step>.from(_runner.steps);
    final Set<Step> addSteps = newSteps.difference(oldSteps);
    final Set<Step> removeSteps = oldSteps.difference(newSteps);

    if (addSteps.isEmpty && removeSteps.isEmpty) {
      return;
    }

    update(addSteps: addSteps, removeSteps: removeSteps);
  }

  /// Adds a step to the recipe runner.
  void _addStep(final Step step) =>
      _runner.addStep(step, handler.manifestMatcher.selectManifest(step));

  /// Creates a new module instance from a manifest and an anchor set in the
  /// graph, identified by several session matches.
  ModuleInstance newModuleInstance(
      final Step step,
      final SessionMatch scopeMatch,
      final List<SessionMatch> anchorMatches,
      final Manifest manifest) {
    final ModuleInstance instance = new ModuleInstance(
        step,
        scopeMatch,
        anchorMatches,
        manifest,
        manifest != null ? _runnerFactory() : null,
        this,
        _inspector);
    _moduleInstances.add(instance);
    return instance;
  }

  void destroyModuleInstance(final ModuleInstance instance) {
    _moduleInstances.remove(instance);
    instance.destroy();
  }
}

// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:modular_core/log.dart';
import 'package:parser/recipe.dart' show Step;
import 'package:parser/manifest.dart';

import 'module.dart';
import 'module_instance.dart';

import 'session.dart';
import 'session_match.dart';
import 'session_pattern.dart';
import 'package:handler/inspector_json_server.dart';

/// The recipe runner matches steps of the recipe of a session against the graph
/// of a session. It is notified of changes to the graph, and on every such
/// change computes two things:
///
/// 1. For which step in the recipe are inputs matched such that a (or possibly
///    another) module instance can be created, and creates the instance. The
///    methods in this class are responsible for this.
///
/// 2. For which existing module instances the graph change affects any of its
///    inputs or outputs, and notifies the module instance of the update. The
///    primary responsibility of the class hierarchy of Module -> SessionPattern
///    -> SessionMatch is to track which module instance was affected by a graph
///    update.
///
/// The invocation of a module is delegated back to the session.
///
/// The resolution of recipe steps to modules is done by the owning session at
/// the time the step is added to the session configuration.
class RecipeRunner {
  final Logger _log = log("RecipeRunner");

  /// The session in which the recipe is run.
  final Session _session;

  /// One entry for each step in the recipe; keeps track of the association of
  /// the data in the session graph and the module instances that produce and
  /// consume them.
  final List<Module> _modules = <Module>[];

  final InspectorJSONServer _inspector;

  RecipeRunner(this._session, this._inspector);

  // TODO(mesch): Only for simulator.
  List<Module> get modules => _modules;

  /// Adds a single step together with a module manifest to the current recipe.
  /// The module is instantiated once there are matches in the session graph.
  /// Matching and instantiation happens on the next call of update().
  void addStep(final Step step, final Manifest manifest) {
    _modules.add(new Module(step, manifest, _session.root, _inspector));
  }

  /// Removes a single step from the recipe. Shuts down all module instances
  /// created for that step.
  void removeStep(final Step step) {
    for (int i = _modules.length - 1; i >= 0; --i) {
      final Module module = _modules[i];
      if (module.step == step) {
        for (final ModuleInstance instance in module.activeInstances) {
          _session.destroyModuleInstance(instance);
        }
        _modules.removeAt(i);
      }
    }
  }

  /// Removes all steps from the current recipe. Shuts down all module
  /// instances.
  void clear() {
    for (final Module module in _modules) {
      for (final ModuleInstance instance in module.activeInstances) {
        _session.destroyModuleInstance(instance);
      }
    }
    _modules.clear();
  }

  /// All the steps in the recipe currently running.
  Iterable<Step> get steps => _modules.map((Module module) => module.step);

  /// This method is called by the session on every graph change. It allows the
  /// runner to adjust the module instance configuration according to the
  /// changed graph.
  void updateMatches(final SessionMatchUpdate updateData) {
    for (final Module module in _modules) {
      module.deleteEdges(updateData);
      _clearInstances(module);
    }

    // This determines which session matches of the recipe path expressions are
    // affected by the current graph change. SessionMatch instances affected are
    // marked dirty (by setting the hasNewData property of SessionMatch).
    for (final Module module in _modules) {
      module.updateMatches(updateData);
    }

    // This determines whether any recipe step has now more matching inputs than
    // it used to and creates instances of the resolved module of these steps as
    // needed.
    for (final Module module in _modules) {
      _instantiate(module);
    }
  }

  /// Cleans up empty matches of scope, inputs, and outputs. Resets the dirty
  /// flags.
  void clearMatches() {
    for (final Module module in _modules) {
      module.clearMatches();
    }
  }

  // Checks for instances that should be terminated because their inputs or
  // scope were deleted.
  void _clearInstances(final Module module) {
    for (int i = 0; i < module.instances.length; ++i) {
      final ModuleInstance instance = module.instances[i];
      if (instance == null) {
        continue;
      }

      // TODO(mesch): At the time this is executed, inputMatches are not yet
      // updated.
      if (instance.anchorMatches
              .any((final SessionMatch match) => !match.isComplete) ||
          (instance.scopeMatch != null && !instance.scopeMatch.isComplete)) {
        _session.destroyModuleInstance(instance);
        module.instances[i] = null;
      }
    }
  }

  /// Creates the actual module instances for the matching inputs of the recipe
  /// steps found by updateMatches(), above.
  void _instantiate(final Module module) {
    // A module without scope and inputs has exactly one instance. This instance
    // cannot go away by disappearing matches, because there aren't any.
    if (module.scope == null && module.inputs.isEmpty) {
      module.instances.length = 1;
      if (module.instances[0] == null) {
        module.instances[0] = _session.newModuleInstance(
            module.step, null, <SessionMatch>[], module.manifest);
      }
      return;
    }

    final int size = module.scope != null ? module.scope.matches.length : 1;

    // By setting instances length here, determines how many instances can be
    // there. Because of the way matches are updated, the size can only have
    // grown since the last invocation.
    module.instances.length = size;

    for (int i = 0; i < size; ++i) {
      if (module.instances[i] != null) {
        continue;
      }

      // TODO(mesch): Also add output matches to anchor matches?
      final List<SessionMatch> anchorMatches = <SessionMatch>[];
      for (final SessionPattern input in module.inputs) {
        if (module.scope != null &&
            module.scope.pathExpr.isPrefixOf(input.pathExpr)) {
          // If the pathExpr has the same prefix, then it still may not contain
          // exactly the same matches in the same order because segments after
          // the common prefix may be repeated. So we look for the first match
          // of the input that matches the same nodes as the scope match where
          // they overlap.
          bool found = false;
          for (final SessionMatch inputMatch in input.matches) {
            if (module.scope.matches[i].isPrefixOf(inputMatch)) {
              if (!found) {
                found = true;
                anchorMatches.add(inputMatch);
              } else if (inputMatch.isComplete) {
                _log.info('Ignoring extra input match: ${input.pathExpr}');
              }
            }
          }
        } else {
          // There is always one match, which however may be partial. We check
          // this below.
          anchorMatches.add(input.matches[0]);
          for (int j = 1; j < input.matches.length; ++j) {
            if (input.matches[j].isComplete) {
              _log.info('Ignoring extra input match: ${input.pathExpr}');
            }
          }
        }
      }

      // Instantiate only if the scope match if there is one and all matches we
      // found for the inputs are complete.
      if ((module.scope != null && !module.scope.matches[i].isComplete)) {
        continue;
      }

      if (anchorMatches.any((final SessionMatch match) => !match.isComplete)) {
        continue;
      }

      module.instances[i] = _session.newModuleInstance(
          module.step,
          module.scope != null ? module.scope.matches[i] : null,
          anchorMatches,
          module.manifest);
    }

    // Cleanup destroyed instances with empty matches. The first match (and the
    // slot for the first instance, even if null) is never removed, because the
    // first match is always reused when it's empty. Also, we do this to prevent
    // unbounded growth of matches, so a fixed number of retained instances
    // doesn't matter.
    for (int i = size - 1; i > 0; --i) {
      // If there is an instance with index > 0, then there must be a scope
      // match, otherwise there wouldn't be more than one instance.
      if (module.instances[i] == null && module.scope.matches[i].isEmpty) {
        module.instances.removeAt(i);
        module.scope.matches.removeAt(i);
      }
    }
  }
}

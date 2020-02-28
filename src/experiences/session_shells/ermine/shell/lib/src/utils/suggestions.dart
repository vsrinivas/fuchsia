// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:fidl_fuchsia_sys_index/fidl_async.dart';
import 'package:fidl_fuchsia_sys/fidl_async.dart';
import 'package:flutter/foundation.dart';
import 'package:fuchsia_services/services.dart';
import 'package:meta/meta.dart';

import 'suggestion.dart';

const _kComponentIndexUrl =
    'fuchsia-pkg://fuchsia.com/component_index#meta/component_index.cmx';

/// Class to encapsulate the functionality of the Suggestions Engine.
class SuggestionService {
  final ComponentIndexProxy _componentIndex;
  final LauncherProxy _launcher;
  final ValueChanged<Suggestion> _onSuggestion;

  /// Constructor.
  const SuggestionService({
    @required ComponentIndex componentIndex,
    @required Launcher launcher,
    ValueChanged<Suggestion> onSuggestion,
  })  : _componentIndex = componentIndex,
        _launcher = launcher,
        _onSuggestion = onSuggestion;

  /// Construct from [StartupContext].
  factory SuggestionService.fromStartupContext({
    StartupContext startupContext,
    ValueChanged<Suggestion> onSuggestion,
  }) {
    final launcher = LauncherProxy();
    startupContext.incoming.connectToService(launcher);

    final Incoming incoming = Incoming();
    final ComponentControllerProxy componentIndexController =
        ComponentControllerProxy();
    launcher.createComponent(
      LaunchInfo(
        url: _kComponentIndexUrl,
        directoryRequest: incoming.request().passChannel(),
      ),
      componentIndexController.ctrl.request(),
    );

    final componentIndex = ComponentIndexProxy();
    incoming
      ..connectToService(componentIndex)
      ..close();

    return SuggestionService(
      componentIndex: componentIndex,
      launcher: launcher,
      onSuggestion: onSuggestion,
    );
  }

  /// Release FIDL services.
  void dispose() {
    _componentIndex.ctrl.close();
    _launcher.ctrl.close();
  }

  /// Returns an [Iterable<Suggestion>] for given [query].
  Future<Iterable<Suggestion>> getSuggestions(String query,
      [int maxSuggestions = 20]) async {
    final newSuggestions = <Suggestion>[];
    // Allow only non empty queries.
    if (query.isEmpty) {
      return newSuggestions;
    }

    final results = await _componentIndex.fuzzySearch(query);
    newSuggestions.addAll(results.map((url) {
      final re = RegExp(r'fuchsia-pkg://fuchsia.com/(.+)#meta/.+');
      final name = re.firstMatch(url)?.group(1);
      return Suggestion(
        id: '$url:${DateTime.now().millisecondsSinceEpoch}',
        url: url,
        title: name,
      );
    }));

    return newSuggestions.take(maxSuggestions);
  }

  /// Invokes the [suggestion].
  Future<void> invokeSuggestion(Suggestion suggestion) async {
    _onSuggestion?.call(suggestion);
  }
}

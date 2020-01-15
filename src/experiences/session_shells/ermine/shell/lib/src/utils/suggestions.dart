// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:fidl_fuchsia_sys_index/fidl_async.dart';
import 'package:fidl_fuchsia_sys/fidl_async.dart';
import 'package:fidl_fuchsia_ui_app/fidl_async.dart';
import 'package:fidl_fuchsia_ui_views/fidl_async.dart';
import 'package:fuchsia_scenic_flutter/child_view_connection.dart';
import 'package:fuchsia_services/services.dart';
import 'package:meta/meta.dart';
import 'package:zircon/zircon.dart';

const _kComponentIndexUrl =
    'fuchsia-pkg://fuchsia.com/component_index#meta/component_index.cmx';

typedef ViewCreatedCallback = void Function(
  String,
  String,
  ChildViewConnection,
);

/// Class to encapsulate the functionality of the Suggestions Engine.
class SuggestionService {
  final ComponentIndexProxy _componentIndex;
  final LauncherProxy _launcher;
  final ViewCreatedCallback _viewCreated;

  /// Constructor.
  const SuggestionService({
    @required ComponentIndex componentIndex,
    @required Launcher launcher,
    ViewCreatedCallback viewCreated,
  })  : _componentIndex = componentIndex,
        _launcher = launcher,
        _viewCreated = viewCreated;

  /// Construct from [StartupContext].
  factory SuggestionService.fromStartupContext({
    StartupContext startupContext,
    ViewCreatedCallback viewCreated,
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
      viewCreated: viewCreated,
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
    final incoming = Incoming();
    final componentController = ComponentControllerProxy();

    await _launcher.createComponent(
      LaunchInfo(
        url: suggestion.url,
        directoryRequest: incoming.request().passChannel(),
      ),
      componentController.ctrl.request(),
    );

    ViewProviderProxy viewProvider = ViewProviderProxy();
    incoming.connectToService(viewProvider);
    await incoming.close();

    final viewTokens = EventPairPair();
    assert(viewTokens.status == ZX.OK);
    final viewHolderToken = ViewHolderToken(value: viewTokens.first);
    final viewToken = ViewToken(value: viewTokens.second);

    await viewProvider.createView(viewToken.value, null, null);
    viewProvider.ctrl.close();

    ChildViewConnection connection = ChildViewConnection(
      viewHolderToken,
      onAvailable: (_) {},
      onUnavailable: (_) {},
    );

    _viewCreated?.call(suggestion.id, suggestion.title, connection);
  }
}

/// Defines a class to hold attributes of a Suggestion.
class Suggestion {
  final String id;
  final String url;
  final String title;

  const Suggestion({this.id, this.url, this.title});
}

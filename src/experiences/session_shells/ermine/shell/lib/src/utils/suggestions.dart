// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:fidl_fuchsia_sys_index/fidl_async.dart';
import 'package:fidl_fuchsia_modular/fidl_async.dart';
import 'package:fidl_fuchsia_sys/fidl_async.dart';
import 'package:fuchsia_services/services.dart';

const _kComponentIndexUrl =
    'fuchsia-pkg://fuchsia.com/component_index#meta/component_index.cmx';

/// Class to encapsulate the functionality of the Suggestions Engine.
class SuggestionService {
  final ComponentIndexProxy _componentIndex;
  final PuppetMasterProxy _puppetMaster;

  /// Constructor.
  const SuggestionService(
    ComponentIndex componentIndex,
    PuppetMaster puppetMaster,
  )   : _componentIndex = componentIndex,
        _puppetMaster = puppetMaster;

  /// Construct from [StartupContext].
  factory SuggestionService.fromStartupContext(StartupContext startupContext) {
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

    final puppetMaster = PuppetMasterProxy();
    startupContext.incoming.connectToService(puppetMaster);

    return SuggestionService(componentIndex, puppetMaster);
  }

  /// Release FIDL services.
  void dispose() {
    _componentIndex.ctrl.close();
    _puppetMaster.ctrl.close();
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
        id: url,
        title: name,
      );
    }));

    return newSuggestions.take(maxSuggestions);
  }

  /// Invokes the [suggestion].
  Future<void> invokeSuggestion(Suggestion suggestion) async {
    final storyMaster = StoryPuppetMasterProxy();
    await _puppetMaster.controlStory(
      suggestion.title,
      storyMaster.ctrl.request(),
    );
    final addMod = AddMod(
      intent: Intent(action: '', handler: suggestion.id),
      surfaceParentModName: [],
      modName: ['root'],
      surfaceRelation: SurfaceRelation(),
    );
    await storyMaster.enqueue([
      StoryCommand.withAddMod(addMod),
      StoryCommand.withSetFocusState(SetFocusState(focused: true)),
    ]);
    await storyMaster.execute();
  }
}

/// Defines a class to hold attributes of a Suggestion.
class Suggestion {
  final String id;
  final String title;

  const Suggestion({this.id, this.title});
}

// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:fidl_fuchsia_app_discover/fidl_async.dart'
    show InteractionType, Suggestion, Suggestions, SuggestionsIteratorProxy;
export 'package:fidl_fuchsia_app_discover/fidl_async.dart' show Suggestion;
export 'package:fidl_fuchsia_modular/fidl_async.dart' show DisplayInfo;

/// Class to encapsulate the functionality of the Suggestions Engine.
class SuggestionService {
  final Suggestions _suggestions;

  /// Constructor.
  const SuggestionService(Suggestions suggestions) : _suggestions = suggestions;

  /// Returns an [Iterable<Suggestion>] for given [query].
  Future<Iterable<Suggestion>> getSuggestions(String query,
      [int maxSuggestions = 20]) async {
    final iterator = SuggestionsIteratorProxy();
    await _suggestions.getSuggestions(query, iterator.ctrl.request());

    final newSuggestions = <Suggestion>[];
    Iterable<Suggestion> partialResults;
    while ((partialResults = await iterator.next()).isNotEmpty &&
        newSuggestions.length < maxSuggestions) {
      newSuggestions.addAll(partialResults);
    }
    return newSuggestions.take(maxSuggestions);
  }

  /// Invokes the [suggestion].
  Future<void> invokeSuggestion(Suggestion suggestion) async {
    return _suggestions.notifyInteraction(
        suggestion.id, InteractionType.selected);
  }
}

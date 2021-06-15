// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter/foundation.dart';
import 'package:meta/meta.dart';

import 'suggestion.dart';

/// Class to encapsulate the functionality of the Suggestions Engine.
class SuggestionService {
  final ValueChanged<Suggestion> _onSuggestion;

  /// Constructor.
  const SuggestionService({
    @required ValueChanged<Suggestion> onSuggestion,
  }) : _onSuggestion = onSuggestion;

  /// Returns an [Iterable<Suggestion>] for given [query].
  Future<Iterable<Suggestion>> getSuggestions(String query,
      [int maxSuggestions = 20]) async {
    final now = DateTime.now().millisecondsSinceEpoch;
    // Allow only non empty queries.
    if (query.isEmpty) {
      return <Suggestion>[];
    } else if (query.startsWith('fuchsia-pkg') || query.startsWith('linux')) {
      return <Suggestion>[
        Suggestion(
          id: '$query:$now',
          url: query,
          title: query,
        )
      ];
    } else {
      // Generate two suggestions, for v1 (.cmx) and v2 (.cml) components.
      return <Suggestion>[
        Suggestion(
          id: '$query-1:$now',
          url: 'fuchsia-pkg://fuchsia.com/$query#meta/$query.cmx',
          title: query,
        ),
        Suggestion(
          id: '$query-2:$now',
          url: 'fuchsia-pkg://fuchsia.com/$query#meta/$query.cml',
          title: query,
        ),
      ];
    }
  }

  /// Invokes the [suggestion].
  Future<void> invokeSuggestion(Suggestion suggestion) async {
    _onSuggestion?.call(suggestion);
  }
}

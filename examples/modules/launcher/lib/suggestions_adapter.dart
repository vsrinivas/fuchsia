// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';

import 'package:modular/log.dart' as logging;
import 'package:modular_services/suggestinator/suggestions.mojom.dart' as mojom;

typedef void SuggestionsUpdatedCallback();

/// [SuggestionsAdapter] abstracts over the Suggestions mojo service. The
/// purpose of this is to prevent having multiple SuggestionObserver mojo stubs
/// in the same module. We instead register one here and route the observer
/// event to internal listeners. This class also maintains a cache of
/// suggestions locally to avoid unneeded round-trips.
class SuggestionsAdapter implements mojom.SuggestionObserver {
  final logging.Logger _log = logging.log('Launcher - SuggestionsAdapter');
  final mojom.SuggestionServiceProxy _proxy;
  final List<SuggestionsUpdatedCallback> _listeners =
      <SuggestionsUpdatedCallback>[];
  final List<mojom.Suggestion> suggestions = <mojom.Suggestion>[];

  SuggestionsAdapter(this._proxy) {
    assert(_proxy.ctrl.isBound);
    final mojom.SuggestionObserverStub observerStub =
        new mojom.SuggestionObserverStub.unbound()..impl = this;
    _proxy.addObserver(observerStub);
  }

  void addListener(SuggestionsUpdatedCallback listener) {
    assert(listener != null);
    _listeners.add(listener);
  }

  void removeListener(SuggestionsUpdatedCallback listener) {
    assert(listener != null);
    _listeners.remove(listener);
  }

  Future<String> selectSuggestion(final mojom.Suggestion suggestion) {
    assert(_proxy.ctrl.isBound);
    Completer<String> completer = new Completer<String>();
    _proxy.selectSuggestion(suggestion.id, (final String sessionId) {
      if (sessionId == null) {
        _log.warning('selectSuggestion failed');
      } else {
        _log.info('selectSuggestion - resulting session: $sessionId');
      }

      completer.complete(sessionId);
    });

    return completer.future;
  }

  @override // mojom.SuggestionObserver
  void onSuggestionsUpdated(List<mojom.Suggestion> suggestions) {
    this.suggestions.clear();
    this.suggestions.addAll(suggestions);
    _listeners.forEach((SuggestionsUpdatedCallback callback) {
      callback();
    });
  }
}

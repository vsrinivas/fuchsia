// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';

import 'package:modular_core/log.dart';
import 'package:common/uuid_mojo_helpers.dart';
import 'package:modular_core/uuid.dart';
import 'package:modular_services/suggestinator/suggestions.mojom.dart' as mojo;
import 'package:modular_services/common/uuid.mojom.dart' as mojo;
import 'package:mojo/core.dart';
import 'package:suggestinator/suggestinator.dart';
import 'package:suggestinator/suggestion.dart';

/// Implements the [SuggestionService] interface.
class SuggestionServiceImpl extends mojo.SuggestionService {
  static final Logger _log = log('suggestinator.SuggestionServiceImpl');

  // List of remote observers. We automatically remove these from this list if
  // the connection to the proxy's owner is severed for any reason.
  List<mojo.SuggestionObserverProxy> _remoteObservers =
      <mojo.SuggestionObserverProxy>[];

  final Suggestinator _suggestinator;

  SuggestionServiceImpl(
      final MojoMessagePipeEndpoint endpoint, this._suggestinator) {
    assert(endpoint != null);
    assert(_suggestinator != null);
    _suggestinator.observer = _onSuggestionsUpdated;
    new mojo.SuggestionServiceStub.fromEndpoint(endpoint, this);
  }

  @override // Suggestions
  void addObserver(final Object observerProxyObject) {
    final mojo.SuggestionObserverProxy observerProxy =
        observerProxyObject as mojo.SuggestionObserverProxy;
    _log.info('addObserver');

    // Add the remote proxy to our list and set it up so that it gets removed if
    // the associated pipe gets disconnected.
    _remoteObservers.add(observerProxy);
    observerProxy.ctrl.errorFuture.then((dynamic error) {
      _log.warning('Proxy error: $error');
      _remoteObservers.remove(observerProxy);
    });

    // TODO(armansito): Change onSuggestionsUpdated to only send a diff of
    // added/removed suggestions rather than the full list.
    // Send initial observer event with the current contents of the list.
    observerProxy.onSuggestionsUpdated(_getMojomSuggestions());
  }

  @override // Suggestions
  Future<Null> selectSuggestion(
      final mojo.Uuid suggestionId, void callback(String sessionId)) async {
    Uuid sessionId = await _suggestinator
        .selectSuggestion(UuidMojoHelpers.fromMojom(suggestionId));
    callback(sessionId == null ? null : sessionId.toBase64());
  }

  /// If |uri| is on host tq.mojoapps.io, this method replaces the hostname with
  /// the correct hostname and port for development builds.
  ///
  /// TODO(armansito): This logic is wrong for modules that are developed in
  /// third-party modules. The code will point to the wrong URL if the
  /// icon/manifest is served outside of the modular repo and tq.mojoapps.io. We
  /// need a good development flow for locally hosting deployed assets and
  /// having them resolve properly.
  Uri _getModularUri(final Uri uri) {
    if (uri == null) return null;
    if (uri.host != 'tq.mojoapps.io') return uri;
    return Uri.base.resolve(uri.path);
  }

  void _onSuggestionsUpdated() {
    for (final mojo.SuggestionObserverProxy o in _remoteObservers) {
      o.onSuggestionsUpdated(_getMojomSuggestions());
    }
  }

  List<mojo.Suggestion> _getMojomSuggestions() {
    List<Suggestion> suggestions = _suggestinator.suggestions.toList()
      ..sort((s1, s2) => s1.compareRelevance(s2));
    return suggestions.map((Suggestion s) {
      final BasicEmbodiment e = s.basicEmbodiment;
      final Uri iconUrl = _getModularUri(e.iconUri);
      return new mojo.Suggestion()
        ..id = UuidMojoHelpers.toMojom(s.id)
        ..description = e.description
        ..createsNewSession = s.createsNewSession
        ..iconUrl = iconUrl?.toString()
        ..themeColor = e.themeColor ?? -1;
    }).toList();
  }
}

// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';

import 'package:common/mojo_uri_loader.dart';
import 'package:handler/inspector_json_server.dart';
import 'package:indexer_client/indexer_client.dart';
import 'package:modular_core/log.dart';
import 'package:modular/modular/handler.mojom.dart';
import 'package:modular_services/suggestinator/suggestions.mojom.dart';
import 'package:mojo/application.dart';
import 'package:mojo/core.dart';
import 'package:parser/manifest.dart';
import 'package:suggestinator/legacy_demo_provider.dart';
import 'package:suggestinator/suggestinator.dart';

import 'session_state_manager_mojo.dart';
import 'suggestion_service.dart';

const int DEBUG_SERVER_PORT = 1843;

class SuggestinatorApplication extends Application {
  static final Logger _log = log('suggestinator.mojo.SuggestinatorApplication');
  final Completer<Suggestinator> _suggestinatorCompleter =
      new Completer<Suggestinator>();
  Future<Suggestinator> _suggestinator;

  SuggestinatorApplication.fromHandle(final MojoHandle handle)
      : super.fromHandle(handle) {
    _suggestinator = _suggestinatorCompleter.future;
  }

  @override // Application
  Future initialize(final List<String> args, final String url) async {
    _log.info('Suggestinator mojo app initialized - url: $url, args: $args');

    assert(_suggestinatorCompleter != null);

    // Connect to the handler and request the Handler and Graph services, which
    // we need.
    final ApplicationConnection handlerConnection =
        connectToApplication('https://tq.mojoapps.io/handler.mojo');
    final HandlerServiceProxy handlerService =
        new HandlerServiceProxy.unbound();
    final SessionGraphServiceProxy graphService =
        new SessionGraphServiceProxy.unbound();
    handlerConnection.requestService(handlerService);
    handlerConnection.requestService(graphService);

    // Load the manifest index.
    List<Manifest> manifestIndex = await new IndexerClient(
            Uri.parse(url), new MojoUriLoader(connectToService))
        .initializeAndGetIndex();

    _suggestinatorCompleter.complete(new Suggestinator(
        new SessionStateManagerMojo(handlerService, graphService),
        [new LegacyDemoProvider()],
        manifestIndex,
        inspector: new InspectorJSONServer(DEBUG_SERVER_PORT)));
  }

  @override // Application
  void acceptConnection(final String requestorUrl, final String resolvedUrl,
      final ApplicationConnection connection) {
    // We only expect connections from the launcher.
    if (!requestorUrl.endsWith('launcher.flx')) {
      _log.warning('Unexpected connection request from: $requestorUrl');
      return;
    }
    connection.provideService(SuggestionService.serviceName,
        (final MojoMessagePipeEndpoint endpoint) async {
      final Suggestinator suggestinator = await _suggestinator;
      new SuggestionServiceImpl(endpoint, suggestinator);
    });
  }
}

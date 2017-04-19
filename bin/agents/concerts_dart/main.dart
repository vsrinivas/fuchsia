// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:convert';

import 'package:application.lib.app.dart/app.dart';
import 'package:apps.maxwell.services.context/context_provider.fidl.dart';
import 'package:lib.fidl.dart/bindings.dart';

/// The Concerts agents will subscribe to context from Spotify and will propose
/// concert modules based on the context

/// Topic that the music modules publishes to
const String _kMusicTopic = 'spotify';

/// Key used for storing the artist ID in context store
const String _kArtistKey = 'spotify:artistId';

/// Stub ContextListener that prints any context updates
class ContextListenerImpl extends ContextListener {
  final _binding = new ContextListenerBinding();

  /// Gets the [InterfaceHandle]
  ///
  /// The returned handle should only be used once.
  InterfaceHandle<ContextListener> getHandle() => _binding.wrap(this);

   @override
   void onUpdate(ContextUpdate result) {
     if(result.values.containsKey(_kMusicTopic)) {
       Map<String, String> data = JSON.decode(result.values[_kMusicTopic]);
       print('[concerts_agent] artist update: ${data[_kArtistKey]}');
     }
   }
}

void main(List args) {
  final ApplicationContext context = new ApplicationContext.fromStartupInfo();
  final ContextProviderProxy provider = new ContextProviderProxy();
  connectToService(context.environmentServices, provider.ctrl);
  ContextQuery query = new ContextQuery.init(<String>[_kMusicTopic]);
  provider.subscribe(query, new ContextListenerImpl().getHandle());
  provider.ctrl.close();
  context.close();
}

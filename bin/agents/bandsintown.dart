// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:apps.maxwell.services.context/client.fidl.dart';
import 'package:apps.maxwell.services.context/subscriber_link.fidl.dart';
import 'package:apps.modular.lib.app.dart/app.dart';
import 'package:lib.fidl.dart/bindings.dart';

class ContextSubscriberLinkImpl extends ContextSubscriberLink {
  final _binding = new ContextSubscriberLinkBinding();
  final Function impl;

  ContextSubscriberLinkImpl(this.impl);

  /// Gets the [InterfaceHandle] for this [LinkWatcher] implementation.
  ///
  /// The returned handle should only be used once.
  InterfaceHandle<ContextSubscriberLink> getHandle() => _binding.wrap(this);

  @override
  void onUpdate(ContextUpdate update) => impl(update);
}

void main(List args) {
  final context = new ApplicationContext.fromStartupInfo();
  final sub = new ContextSubscriberProxy();
  connectToService(context.environmentServices, sub.ctrl);
  final albumIdSub = new ContextSubscriberLinkImpl(print);
  sub.subscribe(
      "album id",
      "https://developer.spotify.com/web-api/user-guide/#spotify-uris-and-ids",
      albumIdSub.getHandle());
  sub.ctrl.close();
  context.close();
}

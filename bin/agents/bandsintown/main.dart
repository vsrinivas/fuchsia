// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:application.lib.app.dart/app.dart';
import 'package:apps.maxwell.lib.context.dart/subscriber_link_impl.dart';
import 'package:apps.maxwell.services.context/client.fidl.dart';

final artistSub = new ContextSubscriberLinkImpl(print);

void main(List args) {
  final context = new ApplicationContext.fromStartupInfo();
  final sub = new ContextSubscriberProxy();
  connectToService(context.environmentServices, sub.ctrl);
  sub.subscribe(
      'music artist id',
      'https://musicbrainz.org/doc/MusicBrainz_Identifier',
      artistSub.getHandle());
  sub.ctrl.close();
  context.close();
}

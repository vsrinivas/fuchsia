// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:application.lib.app.dart/app.dart';
import 'package:apps.maxwell.lib.context.dart/maxwell_context.dart'
    as maxwell_context;
import 'package:apps.maxwell.lib.context.dart/subscriber_link_impl.dart';
import 'package:apps.maxwell.services.context/subscriber_link.fidl.dart';

import 'api.dart';
import 'artist_event.dart';

ContextSubscriberLinkImpl _artistSub;

void main(List args) {
  final context = new ApplicationContext.fromStartupInfo();
  maxwell_context.connectSubscriber(context);
  // TODO(rosswang): Use artist name or ID more intelligently (understand when
  // artist ID is derived from name by MusicBrainz).
  _artistSub = maxwell_context.subscriberLink(
      'music artist id', 'https://musicbrainz.org/doc/MusicBrainz_Identifier',
      (ContextUpdate artistId) async {
    if (artistId.jsonValue != null) {
      try {
        final List<ArtistEvent> events =
            await BandsInTownApi.getEventsForArtist(artistId.jsonValue);
        for (final event in events) {
          print(event);
        }
      } catch (e) {
        print(e);
      }
    }
  });
  maxwell_context.closeGlobals();
  context.close();
}

// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';
import 'package:application.lib.app.dart/app.dart';
import 'package:apps.maxwell.lib.context.dart/maxwell_context.dart'
    as maxwell_context;
import 'package:apps.maxwell.lib.context.dart/subscriber_link_impl.dart';
import 'package:apps.maxwell.services.context/subscriber_link.fidl.dart';

import 'api.dart';
import 'artist_event.dart';

ContextSubscriberLinkImpl _artistMbidSub, _artistNameSub;

Future _processEvents(Future<List<ArtistEvent>> events) async {
  try {
    for (final event in await events) {
      print("${event.datetime} at ${event.venue.name} in ${event.venue.city}");
    }
  } catch (e) {
    print(e);
  }
}

void main(List args) {
  final context = new ApplicationContext.fromStartupInfo();
  maxwell_context.connectSubscriber(context);
  // TODO(rosswang): Use artist name or ID more intelligently (understand when
  // artist ID is derived from name by MusicBrainz).
  // TODO(rosswang): Merge these streams and evaluate usefulness of results.
  _artistMbidSub = maxwell_context.subscriberLink(
      'music artist id', 'https://musicbrainz.org/doc/MusicBrainz_Identifier',
      (ContextUpdate artistId) {
    if (artistId.jsonValue != null) {
      _processEvents(BandsInTownApi.getEventsForArtistMbid(artistId.jsonValue));
    }
  });
  _artistNameSub = maxwell_context.subscriberLink('music artist name', 'string',
      (ContextUpdate artistName) {
    if (artistName.jsonValue != null) {
      _processEvents(
          BandsInTownApi.getEventsForArtistName(artistName.jsonValue));
    }
  });
  maxwell_context.closeGlobals();
  context.close();
}

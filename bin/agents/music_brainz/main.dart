// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:convert';
import 'package:application.lib.app.dart/app.dart';
import 'package:apps.maxwell.lib.context.dart/maxwell_context.dart'
    as maxwell_context;
import 'package:apps.maxwell.services.context/publisher_link.fidl.dart';
import 'package:apps.maxwell.services.context/subscriber_link.fidl.dart';

import 'api.dart';

ContextPublisherLinkProxy _artistIdPub, _socialNetworksPub;

void main(List args) {
  final context = new ApplicationContext.fromStartupInfo();
  maxwell_context.connectPubSub(context);
  _artistIdPub = maxwell_context.buildTransform(
      labelIn: 'music artist name',
      schemaIn: 'string',
      labelOut: 'music artist id',
      schemaOut: 'https://musicbrainz.org/doc/MusicBrainz_Identifier',
      transform: (final ContextUpdate artistName) async {
        final Map<String, dynamic> artistInfo =
            await getArtistInfo(artistName.jsonValue);
        return artistInfo == null ? null : artistInfo['id'];
      });

  _socialNetworksPub = maxwell_context.buildTransform(
      labelIn: 'music artist id',
      schemaIn: 'https://musicbrainz.org/doc/MusicBrainz_Identifier',
      labelOut: 'music artist social networks',
      schemaOut: 'set<url>',
      transform: (final ContextUpdate artistId) async {
        List<String> socialNetworks =
            await getSocialNetworksFromArtistId(artistId.jsonValue);
        return socialNetworks == null ? null : JSON.encode(socialNetworks);
      });
  context.close();
}

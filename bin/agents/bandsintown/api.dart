// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';
import 'dart:convert' show JSON;
import 'package:flutter/http.dart' as http;

import "artist_event.dart";

class BandsInTownApi {
  // Get events for given artist (identified by Music Brainz Id)
  static Future<List<ArtistEvent>> getEventsForArtist(
      String musicBrainzId) async {
    Uri uri = new Uri.https(
        'api.bandsintown.com',
        'artists/mbid_${musicBrainzId}/events',
        {'format': 'json', 'app_id': 'foo', 'api_version': '2.0'});
    http.Response response = await http.get(uri);
    List<Map<String, dynamic>> json = JSON.decode(response.body);
    return json.map((artistEvent) {
      return new ArtistEvent.fromJson(artistEvent);
    }).toList();
  }
}

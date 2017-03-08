// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';
import 'dart:convert' show JSON;
import 'package:http/http.dart' as http;

import "artist_event.dart";

class BandsInTownApi {
  // Get events for given artist (identified by Music Brainz Id)
  static Future<List<ArtistEvent>> getEventsForArtist(String artistRep) async {
    Uri uri = new Uri.https('api.bandsintown.com', 'artists/$artistRep/events',
        {'format': 'json', 'app_id': 'foo', 'api_version': '2.0'});
    http.Response response;
    dynamic decoded;
    try {
      response = await http.get(uri);
      decoded = JSON.decode(response.body);
    } catch (e) {
      print("Failed to parse JSON from $uri: ${response.body}");
      throw e;
    }

    if (decoded is Map && decoded.containsKey("errors")) {
      print("Errors from $uri");
      throw decoded["errors"];
    } else {
      try {
        List<Map<String, dynamic>> json = decoded;
        return json.map((artistEvent) {
          return new ArtistEvent.fromJson(artistEvent);
        }).toList();
      } catch (e) {
        print("Unexpected response from $uri: ${response.body}");
        throw e;
      }
    }
  }

  static Future<List<ArtistEvent>> getEventsForArtistMbid(
      String musicBrainzId) {
    return getEventsForArtist("mbid_$musicBrainzId");
  }

  static Future<List<ArtistEvent>> getEventsForArtistName(String artistName) {
    // TODO(rosswang): Uri.https demands an unencoded path; we should probably
    // use a different Uri constructor and encode here instead.
    return getEventsForArtist(artistName);
  }
}

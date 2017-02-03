// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';
import 'dart:convert';

import 'package:flutter/http.dart' as http;

const _retryCount = 5;
const _retryDelay = 1;

/// https://wiki.musicbrainz.org/Development/JSON_Web_Service
///
/// If the call fails, it will retry up to 4 more times at a 1 second delay.
Future<String> callApi(String path, Map<String, String> params) async {
  Uri uri = new Uri.https('musicbrainz.org', path, params);
  Map<String, String> headers = <String, String>{
    'User-Agent': 'ArtistInfoRetriever/1.0.0'
  };

  for (int i = 1; i <= _retryCount; ++i) {
    if (i > 1) {
      print('Retrying...($i)');
    }

    String responseBody = (await http.get(uri, headers: headers)).body;
    if (responseBody != null) {
      return responseBody;
    }

    // Wait for a while.
    await new Future.delayed(new Duration(seconds: _retryDelay));
  }

  print('WARNING: Failed to retreive data from MusicBrainz.org');
  return null;
}

/// https://wiki.musicbrainz.org/Development/JSON_Web_Service#Artist
Future<Map<String, dynamic>> getArtistInfo(String artistName) async {
  var responseBody =
      await callApi('ws/2/artist', {'query': artistName, 'fmt': 'json'});

  if (responseBody == null) {
    return null;
  }

  Map<String, dynamic> json = JSON.decode(responseBody);
  return json['artists'].elementAt(0);
}

/// https://musicbrainz.org/relationship/99429741-f3f6-484b-84f8-23af51991770
Future<List<String>> getSocialNetworksFromArtistId(String id) async {
  var responseBody =
      await callApi('ws/2/artist/${id}', {'inc': 'url-rels', 'fmt': 'json'});

  if (responseBody == null) {
    return null;
  }

  Map<String, dynamic> json = JSON.decode(responseBody);
  List<Map<String, dynamic>> relations = json['relations'];
  List<String> socialNetworks = [];
  relations.forEach((relation) {
    if (relation['type'] == 'social network') {
      socialNetworks.add(relation['url']['resource']);
    }
  });
  return socialNetworks;
}

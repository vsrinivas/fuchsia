// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';
import 'dart:io';

import 'package:http/http.dart' as http;
import 'package:meta/meta.dart';

class Link<P> {
  final Uri uri;
  final P payload;

  Link(this.uri, this.payload);

  @override
  String toString() => uri.toString();
}

typedef OnElementVerified<P> = void Function(Link<P> link, bool isValid);

Future<Null> verifyLinks<P>(
    List<Link<P>> links, OnElementVerified<P> callback) async {
  final Map<String, List<Link<P>>> urisByDomain = {};
  // Group URLs by domain in order to handle "too many requests" error on a
  // per-domain basis.
  for (Link<P> link in links) {
    urisByDomain.putIfAbsent(link.uri.authority, () => []).add(link);
  }
  await Future.wait(urisByDomain.keys.map((String domain) =>
      LinkVerifier(urisByDomain[domain], http.Client()).verify(callback)));
  return null;
}

@visibleForTesting
class LinkVerifier<P> {
  final List<Link<P>> links;
  final http.Client client;

  LinkVerifier(this.links, this.client);

  Future<Null> verify(OnElementVerified<P> callback) async {
    for (Link<P> link in links) {
      callback(link, await verifyLink(link));
    }
    return null;
  }

  @visibleForTesting
  Future<bool> verifyLink(Link<P> link) async {
    try {
      for (int i = 0; i < 3; i++) {
        final http.Response response = await client.get(link.uri, headers: {
          HttpHeaders.acceptHeader:
              'text/html,application/xhtml+xml,application/xml,',
        });
        final int code = response.statusCode;
        if (code == HttpStatus.tooManyRequests) {
          final int delay =
              int.tryParse(response.headers['retry-after'] ?? '') ?? 50;
          sleep(Duration(milliseconds: delay));
          continue;
        }

        // Http client doesn't automatically follow 308 (Permanent Redirect).
        if (code == HttpStatus.permanentRedirect) {
          if (response.headers.containsKey('location')) {
            Uri redirectUri =
                Uri.parse(link.uri.origin + response.headers['location']);
            return verifyLink(Link<P>(redirectUri, link.payload));
          }
          return false;
        }

        return code == HttpStatus.ok;
      }
    } on IOException {
      // Properly return an invalid link below instead of crashing.
    }
    return false;
  }
}

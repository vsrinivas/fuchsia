// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:io';

import 'package:doc_checker/link_verifier.dart';
import 'package:http/http.dart';
import 'package:http/testing.dart';
import 'package:test/test.dart';

void main() {
  group('doc_checker link_verifier tests', () {
    test('link to string conversion returns the Uri', () {
      Link<String> link = Link(Uri.parse('https://www.example.com'), 'label');
      expect(link.toString(), equals('https://www.example.com'));
    });

    test('response code 200 is considered valid', () async {
      Client client = MockClient((request) async {
        return Response('', HttpStatus.ok);
      });
      Link<String> link = Link(Uri.parse('https://www.example.com'), 'label');
      List<Link<String>> links = <Link<String>>[];

      LinkVerifier<String> linkVerifier = LinkVerifier(links, client);
      expect(linkVerifier.verifyLink(link), completion(isTrue));
    });

    test('response code 404 is considered invalid', () async {
      Client client = MockClient((request) async {
        return Response('', HttpStatus.notFound);
      });
      Link<String> link = Link(Uri.parse('https://www.example.com'), 'label');
      List<Link<String>> links = <Link<String>>[];

      LinkVerifier<String> linkVerifier = LinkVerifier(links, client);
      expect(linkVerifier.verifyLink(link), completion(isFalse));
    });

    test('link verifier follows redirect', () async {
      final Uri srcUri = Uri.parse('https://www.redirect.com');
      final Uri destUri = Uri.parse('https://www.redirect.com/newpage');
      Client client = MockClient((request) async {
        if (request.url == srcUri) {
          return Response('', HttpStatus.permanentRedirect,
              headers: {'location': '/newpage'});
        } else if (request.url == destUri) {
          return Response('', HttpStatus.ok);
        } else {
          return Response('', HttpStatus.notFound);
        }
      });

      Link<String> link = Link(srcUri, 'label');
      List<Link<String>> links = <Link<String>>[];

      LinkVerifier<String> linkVerifier = LinkVerifier(links, client);
      var valid = await linkVerifier.verifyLink(link);
      expect(valid, isTrue);
    });
  });
}

// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';
import 'dart:convert';

import 'package:modular_core/log.dart';
import 'package:common/uri_loader.dart';
import 'package:indexer_client/indexer_client.dart';
import 'package:logging/logging.dart';
import 'package:parser/expression.dart';
import 'package:parser/manifest.dart';
import 'package:test/test.dart';

const List<String> manifests = const [
  """
verb: v1
output:
 - p1
url: http://tq.io/u1
use:
 - v1: http://tq.io/v1
 - p1: http://tq.io/p1
""",
  """
verb: v2
input:
 - p1
url: http://tq.io/u2
use:
 - v2: http://tq.io/v2
 - p1: http://tq.io/p1
""",
  """
verb: v3
input:
 - p1
 - p2
 - p3
url: http://tq.io/u3
use:
 - v3: http://tq.io/v3
 - p1: http://tq.io/p1
 - p2: http://tq.io/p2
 - p3: http://tq.io/p3
""",
  """
verb: v4
input:
 - p1
 - p2
url: http://tq.io/u4
use:
 - v4: http://tq.io/v4
 - p1: http://tq.io/p1
 - p2: http://tq.io/p2
""",
  """
verb: v5
input:
 - p2
output:
 - p1
url: http://tq.io/u5
use:
 - v5: http://tq.io/v5
 - p1: http://tq.io/p1
 - p2: http://tq.io/p2
""",
  """
verb: v6
input:
 - p3
 - p4
output:
 - p2
url: http://tq.io/u6
use:
 - v6: http://tq.io/v6
 - p2: http://tq.io/p2
 - p3: http://tq.io/p3
 - p4: http://tq.io/p4
""",
  """
verb: v7
input:
 - p5
output:
 - p3
 - p4
url: http://tq.io/u7
use:
 - v7: http://tq.io/v7
 - p3: http://tq.io/p3
 - p4: http://tq.io/p4
 - p5: http://tq.io/p5
""",
  """
verb: v8
output:
 - p1
 - p2
 - p3
url: http://tq.io/u8
use:
 - v8: http://tq.io/v8
 - p1: http://tq.io/p1
 - p2: http://tq.io/p2
 - p3: http://tq.io/p3
""",
  """
verb: v9
output:
 - p5
url: http://tq.io/u9
use:
 - v9: http://tq.io/v9
 - p5: http://tq.io/p5
""",
  """
verb: v10
input:
 - p6
url: http://tq.io/u10
use:
 - v10: http://tq.io/v10
 - p6: http://tq.io/p6
"""
];

class MockUriLoader implements UriLoader {
  String _index;
  String _auxIndex;

  MockUriLoader(List<String> index, List<String> auxIndex) {
    String makeIndex(List<String> manifestStrings) {
      if (manifestStrings == null) return null;
      List<Manifest> parsedManifests = manifestStrings
          .map((String s) => new Manifest.parseYamlString(s))
          .toList();

      return JSON.encode(parsedManifests);
    }

    _index = makeIndex(index);
    _auxIndex = makeIndex(auxIndex);
  }

  @override
  Future<String> getString(Uri uri) async {
    if (uri.pathSegments.last == 'index.json') {
      return _index;
    } else if (uri.pathSegments.last == 'aux_index.json') {
      return _auxIndex;
    } else {
      return '';
    }
  }
}

void main() {
  List<LogRecord> captureList;

  final List<Verb> verbs = new List<Verb>(manifests.length);
  final List<PathExpr> properties = new List<PathExpr>(manifests.length);
  final List<Uri> urls = new List<Uri>(manifests.length);
  for (var i = 1; i <= manifests.length; ++i) {
    verbs[i - 1] = new Verb(new Label.fromUri(new Uri.http('tq.io', 'v$i')));
    properties[i - 1] = new PathExpr.single(
        new Property([new Label.fromUri(new Uri.http('tq.io', 'p$i'))]));
    urls[i - 1] = new Uri.http('tq.io', 'u$i');
  }

  void expectAllManifests(final List<Manifest> toCheck) {
    expect(toCheck, hasLength(manifests.length));

    expect(toCheck[0].verb, equals(verbs[0]));
    expect(toCheck[0].url, equals(urls[0]));
    expect(toCheck[0].output, equals([properties[0]]));

    expect(toCheck[1].verb, equals(verbs[1]));
    expect(toCheck[1].url, equals(urls[1]));
    expect(toCheck[1].input, equals([properties[0]]));

    expect(toCheck[2].verb, equals(verbs[2]));
    expect(toCheck[2].url, equals(urls[2]));
    expect(toCheck[2].input, equals(properties.sublist(0, 3)));

    expect(toCheck[3].verb, equals(verbs[3]));
    expect(toCheck[3].url, equals(urls[3]));
    expect(toCheck[3].input, equals(properties.sublist(0, 2)));

    expect(toCheck[4].verb, equals(verbs[4]));
    expect(toCheck[4].url, equals(urls[4]));
    expect(toCheck[4].input, equals([properties[1]]));
    expect(toCheck[4].output, equals([properties[0]]));

    expect(toCheck[5].verb, equals(verbs[5]));
    expect(toCheck[5].url, equals(urls[5]));
    expect(toCheck[5].input, equals(properties.sublist(2, 4)));
    expect(toCheck[5].output, equals([properties[1]]));

    expect(toCheck[6].verb, equals(verbs[6]));
    expect(toCheck[6].url, equals(urls[6]));
    expect(toCheck[6].input, equals([properties[4]]));
    expect(toCheck[6].output, equals(properties.sublist(2, 4)));

    expect(toCheck[7].verb, equals(verbs[7]));
    expect(toCheck[7].url, equals(urls[7]));
    expect(toCheck[7].output, equals(properties.sublist(0, 3)));

    expect(toCheck[8].verb, equals(verbs[8]));
    expect(toCheck[8].url, equals(urls[8]));
    expect(toCheck[8].output, equals([properties[4]]));

    expect(toCheck[9].verb, equals(verbs[9]));
    expect(toCheck[9].url, equals(urls[9]));
    expect(toCheck[9].input, equals([properties[5]]));
  }

  group('ParseManifestTests', () {
    setUp(() => captureList = startLogCapture(Level.WARNING));
    tearDown(() => stopLogCapture());

    test('Missing index', () async {
      // We expect a missing index file not to crash on us. Returning 'null'
      // from the MockUriLoader is the same return value we get on error when
      // fetching a live URL.
      final UriLoader uriLoader = new MockUriLoader(null, []);
      final IndexerClient client = new IndexerClient(Uri.parse(""), uriLoader);
      await client.initialize();

      expect(captureList, isNotEmpty);
      expect(client.index, isEmpty);
    });

    test('Valid manifest', () async {
      final UriLoader uriLoader = new MockUriLoader(manifests, []);
      final IndexerClient client = new IndexerClient(Uri.parse(""), uriLoader);
      await client.initialize();

      expect(captureList, isEmpty);
      expectAllManifests(client.index);
    });

    test('Aux Valid manifest', () async {
      final UriLoader uriLoader = new MockUriLoader([], manifests);
      final IndexerClient client = new IndexerClient(Uri.parse(""), uriLoader,
          auxIndex: 'aux_index.json');
      await client.initialize();

      expect(captureList, isEmpty);
      expectAllManifests(client.index);
    });
  });

  group('ManifestQueryTests', () {
    setUp(() => captureList = startLogCapture(Level.WARNING));
    tearDown(() => stopLogCapture());

    test('Match single', () async {
      final UriLoader uriLoader = new MockUriLoader(manifests, []);
      final IndexerClient client = new IndexerClient(Uri.parse(""), uriLoader);
      await client.initialize();

      final ManifestQuery query1 = new ManifestQuery(
          verb: [verbs[0]], input: [], output: [properties[0]]);
      final List<Manifest> queryResult1 = client.getMatchingManifests(query1);

      expect(captureList, isEmpty);
      expect(queryResult1, hasLength(1));
      expect(queryResult1[0].verb, equals(verbs[0]));
      expect(queryResult1[0].url, equals(urls[0]));
      expect(queryResult1[0].output, equals([properties[0]]));

      final ManifestQuery query2 = new ManifestQuery(
          verb: [verbs[1]], input: [properties[0]], output: []);
      final List<Manifest> queryResult2 = client.getMatchingManifests(query2);

      expect(captureList, isEmpty);
      expect(queryResult2, hasLength(1));
      expect(queryResult2[0].verb, equals(verbs[1]));
      expect(queryResult2[0].url, equals(urls[1]));
      expect(queryResult2[0].input, equals([properties[0]]));
    });

    test('Match none', () async {
      final UriLoader uriLoader = new MockUriLoader(manifests, []);
      final IndexerClient client = new IndexerClient(Uri.parse(""), uriLoader);
      await client.initialize();

      final ManifestQuery query =
          new ManifestQuery(verb: [verbs[1]], output: [properties[0]]);
      final List<Manifest> queryResult = client.getMatchingManifests(query);

      expect(captureList, isEmpty);
      expect(queryResult, hasLength(0));
    });

    test('Match all', () async {
      final UriLoader uriLoader = new MockUriLoader(manifests, []);
      final IndexerClient client = new IndexerClient(Uri.parse(""), uriLoader);
      await client.initialize();

      final ManifestQuery query = new ManifestQuery();
      final List<Manifest> queryResult = client.getMatchingManifests(query);

      expect(captureList, isEmpty);
      expectAllManifests(queryResult);
    });

    test('Fuzzy matching', () async {
      final UriLoader uriLoader =
          new MockUriLoader(manifests.sublist(0, 3), []);
      final IndexerClient client = new IndexerClient(Uri.parse(""), uriLoader);
      await client.initialize();

      final ManifestQuery query1 =
          new ManifestQuery(input: properties.sublist(0, 2));
      final List<Manifest> queryResult1 = client.getMatchingManifests(query1);

      expect(captureList, isEmpty);
      expect(queryResult1, hasLength(1));
      expect(queryResult1[0].verb, equals(verbs[2]));
      expect(queryResult1[0].url, equals(urls[2]));
      expect(queryResult1[0].input, equals(properties.sublist(0, 3)));

      final ManifestQuery query2 =
          new ManifestQuery(input: properties.sublist(0, 2), output: []);
      final List<Manifest> queryResult2 = client.getMatchingManifests(query2);

      expect(captureList, isEmpty);
      expect(queryResult2, hasLength(1));
      expect(queryResult2[0].verb, equals(verbs[2]));
      expect(queryResult2[0].url, equals(urls[2]));
      expect(queryResult2[0].input, equals(properties.sublist(0, 3)));

      final ManifestQuery query7 =
          new ManifestQuery(verb: [verbs[2]], output: []);
      final List<Manifest> queryResult7 = client.getMatchingManifests(query7);

      expect(captureList, isEmpty);
      expect(queryResult7, hasLength(1));
      expect(queryResult7[0].verb, equals(verbs[2]));
      expect(queryResult7[0].url, equals(urls[2]));
      expect(queryResult7[0].input, equals(properties.sublist(0, 3)));
    });

    test('Ranked matching', () async {
      final UriLoader uriLoader = new MockUriLoader(manifests, []);
      final IndexerClient client = new IndexerClient(Uri.parse(""), uriLoader);
      await client.initialize();

      final ManifestQuery query1 = new ManifestQuery(
          input: properties.sublist(0, 3), completeMatch: false);
      final List<Manifest> queryResult1 =
          client.getRankedMatchingManifests(query1);

      expect(captureList, isEmpty);
      expect(queryResult1, hasLength(5));
      expect(queryResult1[0].verb, equals(verbs[2]));
      expect(queryResult1[0].url, equals(urls[2]));
      expect(queryResult1[0].input, equals(properties.sublist(0, 3)));
      expect(queryResult1[1].verb, equals(verbs[3]));
      expect(queryResult1[1].url, equals(urls[3]));
      expect(queryResult1[1].input, equals(properties.sublist(0, 2)));
      expect(queryResult1[2].verb, equals(verbs[1]));
      expect(queryResult1[2].url, equals(urls[1]));
      expect(queryResult1[2].input, equals([properties[0]]));
      expect(queryResult1[3].verb, equals(verbs[4]));
      expect(queryResult1[3].url, equals(urls[4]));
      expect(queryResult1[3].input, equals([properties[1]]));
      expect(queryResult1[3].output, equals([properties[0]]));
      expect(queryResult1[4].verb, equals(verbs[5]));
      expect(queryResult1[4].url, equals(urls[5]));
      expect(queryResult1[4].input, equals(properties.sublist(2, 4)));
      expect(queryResult1[4].output, equals([properties[1]]));

      final ManifestQuery query2 = new ManifestQuery(
          output: properties.sublist(0, 3), completeMatch: false);
      final List<Manifest> queryResult2 =
          client.getRankedMatchingManifests(query2);

      expect(captureList, isEmpty);
      expect(queryResult2, hasLength(5));
      expect(queryResult2[0].verb, equals(verbs[7]));
      expect(queryResult2[0].url, equals(urls[7]));
      expect(queryResult2[0].output, equals(properties.sublist(0, 3)));
      expect(queryResult2[1].verb, equals(verbs[0]));
      expect(queryResult2[1].url, equals(urls[0]));
      expect(queryResult2[1].output, equals([properties[0]]));
      expect(queryResult2[2].verb, equals(verbs[4]));
      expect(queryResult2[2].url, equals(urls[4]));
      expect(queryResult2[2].output, equals([properties[0]]));
      expect(queryResult2[3].verb, equals(verbs[5]));
      expect(queryResult2[3].url, equals(urls[5]));
      expect(queryResult2[3].output, equals([properties[1]]));
      expect(queryResult2[4].verb, equals(verbs[6]));
      expect(queryResult2[4].url, equals(urls[6]));
      expect(queryResult2[4].output, equals(properties.sublist(2, 4)));
    });
  });

  group('ResolveInputsTests', () {
    setUp(() => captureList = startLogCapture(Level.WARNING));
    tearDown(() => stopLogCapture());

    test('Nothing to resolve', () async {
      final UriLoader uriLoader = new MockUriLoader(manifests, []);
      final IndexerClient client = new IndexerClient(Uri.parse(""), uriLoader);
      await client.initialize();

      final List<List<Manifest>> resolution = client
          .resolveManifestInputs(new Manifest.parseYamlString(manifests[0]));

      expect(captureList, isEmpty);
      expect(resolution, isEmpty);
    });

    test('Single resolution', () async {
      final UriLoader uriLoader = new MockUriLoader(manifests, []);
      final IndexerClient client = new IndexerClient(Uri.parse(""), uriLoader);
      await client.initialize();

      final List<List<Manifest>> resolution1 = client
          .resolveManifestInputs(new Manifest.parseYamlString(manifests[1]));

      expect(captureList, isEmpty);
      expect(resolution1, hasLength(3));
      expect(resolution1[0][0].verb, equals(verbs[0]));
      expect(resolution1[0][0].url, equals(urls[0]));
      expect(resolution1[0][0].output, equals([properties[0]]));
      expect(resolution1[1][0].verb, equals(verbs[4]));
      expect(resolution1[1][0].url, equals(urls[4]));
      expect(resolution1[1][0].input, equals([properties[1]]));
      expect(resolution1[1][0].output, equals([properties[0]]));
      expect(resolution1[1][1].verb, equals(verbs[7]));
      expect(resolution1[1][1].url, equals(urls[7]));
      expect(resolution1[1][1].output, equals(properties.sublist(0, 3)));
      expect(resolution1[2][0].verb, equals(verbs[7]));
      expect(resolution1[2][0].url, equals(urls[7]));
      expect(resolution1[2][0].output, equals(properties.sublist(0, 3)));

      final List<List<Manifest>> resolution2 = client
          .resolveManifestInputs(new Manifest.parseYamlString(manifests[4]));

      expect(captureList, isEmpty);
      expect(resolution2, hasLength(2));
      expect(resolution2[0][0].verb, equals(verbs[5]));
      expect(resolution2[0][0].url, equals(urls[5]));
      expect(resolution2[0][0].input, equals(properties.sublist(2, 4)));
      expect(resolution2[0][0].output, equals([properties[1]]));
      expect(resolution2[0][1].verb, equals(verbs[6]));
      expect(resolution2[0][1].url, equals(urls[6]));
      expect(resolution2[0][1].input, equals([properties[4]]));
      expect(resolution2[0][1].output, equals(properties.sublist(2, 4)));
      expect(resolution2[0][2].verb, equals(verbs[8]));
      expect(resolution2[0][2].url, equals(urls[8]));
      expect(resolution2[0][2].output, equals([properties[4]]));
      expect(resolution2[1][0].verb, equals(verbs[7]));
      expect(resolution2[1][0].url, equals(urls[7]));
      expect(resolution2[1][0].output, equals(properties.sublist(0, 3)));

      final List<List<Manifest>> resolution3 = client
          .resolveManifestInputs(new Manifest.parseYamlString(manifests[2]));

      expect(captureList, isEmpty);
      expect(resolution3, hasLength(1));
      expect(resolution3[0][0].verb, equals(verbs[7]));
      expect(resolution3[0][0].url, equals(urls[7]));
      expect(resolution3[0][0].output, equals(properties.sublist(0, 3)));

      final List<List<Manifest>> resolution4 = client
          .resolveManifestInputs(new Manifest.parseYamlString(manifests[3]));

      expect(captureList, isEmpty);
      expect(resolution4, hasLength(1));
      expect(resolution4[0][0].verb, equals(verbs[7]));
      expect(resolution4[0][0].url, equals(urls[7]));
      expect(resolution4[0][0].output, equals(properties.sublist(0, 3)));

      final List<List<Manifest>> resolution5 = client
          .resolveManifestInputs(new Manifest.parseYamlString(manifests[6]));

      expect(captureList, isEmpty);
      expect(resolution5, hasLength(1));
      expect(resolution5[0][0].verb, equals(verbs[8]));
      expect(resolution5[0][0].url, equals(urls[8]));
      expect(resolution5[0][0].output, equals([properties[4]]));
    });

    test('No resolution', () async {
      final UriLoader uriLoader = new MockUriLoader(manifests, []);
      final IndexerClient client = new IndexerClient(Uri.parse(""), uriLoader);
      await client.initialize();

      final List<List<Manifest>> resolution1 = client
          .resolveManifestInputs(new Manifest.parseYamlString(manifests[9]));

      expect(captureList, isEmpty);
      expect(resolution1, isEmpty);

      final List<List<Manifest>> resolution2 = client
          .resolveManifestInputs(new Manifest.parseYamlString(manifests[8]));

      expect(captureList, isEmpty);
      expect(resolution2, isEmpty);
    });
  });
}

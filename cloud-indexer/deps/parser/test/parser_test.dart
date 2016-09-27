// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:parser/parse_error.dart';
import 'package:parser/parser.dart';
import 'package:test/test.dart';
import 'package:yaml/yaml.dart';

void main() {
  group('Use section', () {
    test('Error on multiple shorthands for one url', () {
      final String input = '''
        use:
         - something: https://example.com
         - different-thing: https://example.com
      ''';

      YamlMap yaml = loadYaml(input);
      expect(() {
        parseUses(yaml.nodes['use']);
      }, throwsA(new isInstanceOf<ParseError>()));
    });
  });
}

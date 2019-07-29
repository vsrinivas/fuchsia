// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:collection';

import 'package:mockito/mockito.dart';
import 'package:settings/src/models/settings_model.dart'; // ignore: implementation_imports
import 'package:settings/src/setting_entry.dart'; // ignore: implementation_imports
import 'package:settings/src/setting_entry_parser.dart'; // ignore: implementation_imports
import 'package:test/test.dart';

class MockSettingsModel extends Mock implements SettingsModel {}

void main() {
  // Makes sure
  test('test_parsing', () async {
    const String testYaml = '''
---
- display:
    title: Display
    component: display_settings
- accessibility:
    title: Accessibility
    component: accessibility_settings
...
''';

    List<SettingEntry> entries =
        await SettingEntryParser.parse(MockSettingsModel(), testYaml);

    expect(entries.length, 2);

    HashMap<String, SettingEntry> entryMap = HashMap();

    for (SettingEntry entry in entries) {
      entryMap[entry.id] = entry;
    }

    expect(entryMap.containsKey('display'), true);
    expect(entryMap.containsKey('accessibility'), true);

    expect(entryMap['display'].route, '/display');
    expect(entryMap['accessibility'].route, '/accessibility');
  });
}

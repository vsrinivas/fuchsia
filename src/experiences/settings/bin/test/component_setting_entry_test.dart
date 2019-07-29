// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:settings/src/setting_entry.dart'; // ignore: implementation_imports
import 'package:test/test.dart';

void main() {
  // Makes sure
  test('test_construction', () {
    const String id = 'foo';
    const String title = 'bar';
    const String component = 'baz';
    const int icon = 0;
    const String componentPath =
        'fuchsia-pkg://fuchsia.com/$component#meta/$component.cmx';
    ComponentSettingEntry entry =
        ComponentSettingEntry(id, title, icon, component);

    expect(id, entry.id);
    expect('/$id', entry.route);
    expect(componentPath, entry.componentPath);
  });
}

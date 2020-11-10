// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:fidl_fuchsia_ui_remotewidgets/fidl_async.dart';
import 'package:test/test.dart';
import 'package:quickui/quickui.dart';
import 'package:quickui/uistream.dart';

void main() {
  test('Create UiStream', () async {
    final ui = TestUi();
    final uiStream = UiStream(ui)..listen();
    final spec = await uiStream.stream.first;

    final group = spec.groups.first;
    expect(group.title, 'Foo');
    expect(group.values.length, 0);
  });

  test('Update UiStream', () async {
    final ui = TestUi();
    final uiStream = UiStream(ui)..listen();
    final stream = uiStream.stream;
    Spec spec = await stream.first;

    Group group = spec.groups.first;
    expect(group.title, 'Foo');
    expect(group.values.length, 0);

    ui.update(Value.withNumber(NumberValue(
      value: Number.withIntValue(5),
      action: 1,
    )));

    spec = await stream.skip(1).first;

    group = spec.groups.first;
    expect(group.title, 'Bar');
    expect(group.values.length, 1);
    expect(group.values.first.$tag, ValueTag.number);
    expect(group.values.first.number.action, 1);
    expect(group.values.first.number.value.intValue, 5);
  });
}

class TestUi extends UiSpec {
  TestUi() : super(_build());

  @override
  void update(Value value) {
    spec = Spec(
      title: '',
      groups: <Group>[
        Group(title: 'Bar', values: [value]),
      ],
    );
  }

  @override
  void dispose() {}

  //ignore: prefer_constructors_over_static_methods
  static Spec _build() {
    return Spec(title: '', groups: <Group>[Group(title: 'Foo', values: [])]);
  }
}

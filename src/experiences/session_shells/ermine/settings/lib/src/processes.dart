// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:fidl_fuchsia_ui_remotewidgets/fidl_async.dart';
import 'package:quickui/quickui.dart';

/// Defines a [UiSpec] for visualizing processes metrics (mock data).
class Processes extends UiSpec {
  static const _title = 'Top Processes';

  final List<TextValue> _processes = [
    TextValue(text: 'Name'),
    TextValue(text: 'PID'),
    TextValue(text: 'CPU'),
    TextValue(text: 'MEM'),
    TextValue(text: 'IDE'),
    TextValue(text: '6007'),
    TextValue(text: '2.49'),
    TextValue(text: '1.56'),
    TextValue(text: 'Chrome'),
    TextValue(text: '9646'),
    TextValue(text: '1.00'),
    TextValue(text: '3.08'),
    TextValue(text: 'Music'),
    TextValue(text: '5782'),
    TextValue(text: '0.50'),
    TextValue(text: '0.46'),
  ];

  Processes() {
    spec = _specForProcesses(_processes);
  }

  factory Processes.fromStartupContext() {
    return Processes();
  }

  @override
  void update(Value value) async {}

  @override
  void dispose() {}

  static Spec _specForProcesses(List<TextValue> value) {
    return Spec(groups: [
      Group(
          title: _title,
          values: [Value.withGrid(GridValue(columns: 4, values: value))]),
    ]);
  }
}

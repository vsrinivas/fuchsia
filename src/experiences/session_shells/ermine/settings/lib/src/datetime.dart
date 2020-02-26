// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';

import 'package:fidl_fuchsia_ui_remotewidgets/fidl_async.dart';
import 'package:intl/intl.dart';
import 'package:internationalization/strings.dart';
import 'package:quickui/quickui.dart';

/// Defines a [UiSpec] for displaying date and time.
class Datetime extends UiSpec {
  // Localized strings.
  static String get _title => Strings.dateTime;
  static const Duration refreshDuration = Duration(seconds: 1);

  // Action to change timezone.
  static int changeAction = QuickAction.details.$value;

  Timer _timer;

  Datetime() {
    _timer = Timer.periodic(refreshDuration, (_) => _onChange());
    _onChange();
  }

  void _onChange() async {
    spec = _specForDateTime();
  }

  @override
  void update(Value value) async {}

  @override
  void dispose() {
    _timer?.cancel();
  }

  static Spec _specForDateTime([int action = 0]) {
    String dateTime = DateFormat.E().add_yMd().add_jm().format(DateTime.now());
    return Spec(title: _title, groups: [
      Group(title: _title, values: [Value.withText(TextValue(text: dateTime))]),
    ]);
  }
}

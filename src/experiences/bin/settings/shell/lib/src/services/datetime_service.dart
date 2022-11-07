// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';
import 'dart:ui';

import 'package:shell_settings/src/services/task_service.dart';

/// Defines a [TaskService] to update system time displayed in setttings app.
class DateTimeService implements TaskService {
  late final VoidCallback onChanged;

  DateTimeService();

  Timer? _timer;

  @override
  Future<void> start() async {
    await stop();
    // In order to refresh the time at a second level granularity, run a timer
    // at less than a second interval to ensure that the displayed time is never
    // more than a second behind.
    _timer = Timer.periodic(Duration(milliseconds: 899), (_) => onChanged());
  }

  @override
  Future<void> stop() async {
    _timer?.cancel();
  }

  @override
  void dispose() {
    _timer?.cancel();
  }
}

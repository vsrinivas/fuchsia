// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter/material.dart';

import 'package:fidl_fuchsia_timezone/fidl_async.dart' show TimezoneProxy;
import 'package:fuchsia_logger/logger.dart' show setupLogger;
import 'package:fuchsia_services/services.dart' show StartupContext;
import 'package:lib.settings/timezone_picker.dart';

/// Main entry point to the datetime settings module.
Future<void> main() async {
  setupLogger(name: 'datetime_settings');

  final timezone = TimezoneProxy();
  StartupContext.fromStartupInfo().incoming.connectToService(timezone);
  final currentTimezone = ValueNotifier<String>(await timezone.getTimezoneId());

  Widget app = MaterialApp(
    home: Container(
      child: Container(
        color: Colors.white,
        child: AnimatedBuilder(
            animation: currentTimezone,
            builder: (context, child) {
              return TimezonePicker(
                onTap: (tz) {
                  timezone.setTimezone(tz);
                  currentTimezone.value = tz;
                },
                currentTimezoneId: currentTimezone.value,
              );
            }),
      ),
    ),
  );

  runApp(app);
}

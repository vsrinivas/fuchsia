// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:fidl_fuchsia_feedback/fidl_async.dart';
import 'package:fuchsia_logger/logger.dart';
import 'package:fuchsia_services/services.dart';
import 'package:uuid/uuid.dart';
import 'package:zircon/zircon.dart' as zircon;

typedef UserFeedbackCallback = void Function(String url);

class UserFeedbackService {
  late final UserFeedbackCallback onSubmit;
  late final UserFeedbackCallback onError;

  Future<void> submit(String title, String desc, String username) async {
    final uptime = zircon.System.clockGetMonotonic();

    final eventId = Uuid().v4();
    final crashSignature = 'fuchsia-workstation-user-feedback-$eventId';

    final report = CrashReport(
      programName: 'user feedback',
      programUptime: uptime,
      eventId: eventId,
      crashSignature: crashSignature,
      annotations: [
        Annotation(key: 'feedback.description', value: desc),
        Annotation(key: 'feedback.summary', value: title),
        Annotation(key: 'feedback.username', value: username),
      ],
    );

    final reporter = CrashReporterProxy();
    final connection = Incoming.fromSvcPath()..connectToService(reporter);

    try {
      // TODO(fxb/88445): Add the latency handling UX
      await reporter.file(report);
      log.info('Filed a user feedback report with eventID: $eventId');
      onSubmit(eventId);
    } on Exception catch (e) {
      onError(e.toString());
      log.warning('Failed to file user feedback: $e ${StackTrace.current}');
    }
    reporter.ctrl.close();
    await connection.close();
  }
}

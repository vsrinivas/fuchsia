// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(https://fxbug.dev/84961): Fix null safety and remove this language version.
// @dart=2.9

import 'package:logging/logging.dart';

import '../metrics_results.dart';
import '../time_delta.dart';
import '../trace_model.dart';
import 'common.dart';

final _log = Logger('CameraMetricsProcessor');

class _Results {
  int receivedCount;
  int droppedCount;
  TimeDelta totalDuration;
  Map<String, int> frameDropReasonCounts;

  double get averageFps => (receivedCount - 1) / totalDuration.toSecondsF();
}

_Results _computeCameraMetrics(Model model) {
  final frameReceivedEvents = filterEventsTyped<InstantEvent>(
          getAllEvents(model),
          category: 'camera',
          name: 'StreamRecord::FrameReceived')
      .toList();
  final frameDroppedEvents = filterEventsTyped<InstantEvent>(
          getAllEvents(model),
          category: 'camera',
          name: 'StreamRecord::FrameDropped')
      .toList();

  final bothEvents = frameReceivedEvents + frameDroppedEvents;
  final eventTimes = bothEvents.map((e) => e.start).toList()..sort();
  final start = eventTimes.first;
  final end = eventTimes.last;

  if (!(start < end)) {
    throw ArgumentError('Got same start and end time '
        '${start.toEpochDelta().toSecondsF()} for ${bothEvents.length} "camera"'
        ' events');
  }

  final frameDropReasonCounts = <String, int>{};
  getArgValuesFromEvents(frameDroppedEvents, 'reason').forEach(
      (v) => frameDropReasonCounts.update(v, (c) => c + 1, ifAbsent: () => 1));

  return _Results()
    ..receivedCount = frameReceivedEvents.length
    ..droppedCount = frameDroppedEvents.length
    ..totalDuration = end - start
    ..frameDropReasonCounts = frameDropReasonCounts;
}

String _resultsToString(_Results results) {
  return '''
===
Camera Metrics:
===

fps: ${results.averageFps}
  received frames count: ${results.receivedCount}
  dropped frames count: ${results.droppedCount}
  total frames count: ${results.receivedCount + results.droppedCount}
  total work duration (seconds): ${results.totalDuration.toSecondsF()}
  frameDropReasonCounts: ${results.frameDropReasonCounts}
''';
}

List<TestCaseResults> cameraMetricsProcessor(
    Model model, Map<String, dynamic> extraArgs) {
  final results = _computeCameraMetrics(model);
  _log.info(_resultsToString(results));
  return [
    TestCaseResults('camera_fps', Unit.framesPerSecond, [results.averageFps]),
  ];
}

String cameraMetricsReport(Model model) {
  final results = _computeCameraMetrics(model);
  return _resultsToString(results);
}

// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:convert' show json, LineSplitter, utf8;
import 'dart:io' show Platform, Process, ProcessSignal;

import 'package:meta/meta.dart' show visibleForTesting;

import 'dump.dart';
import 'performance.dart';
import 'trace_processing/metrics_results.dart';

class ZedmonException implements Exception {
  final String message;

  ZedmonException(this.message);

  @override
  String toString() => 'ZedmonException: $message';
}

/// Summarizes the power measurements provided by a [_ZedmonProcess].
class PowerSummary {
  /// Average power (Watts) over the trace interval.
  final double averagePower;

  /// Duration of the trace interval.
  final Duration duration;

  PowerSummary(this.averagePower, this.duration);

  @override
  String toString() => '{ averagePower: $averagePower, duration: $duration }';
}

/// Individual timepoint of zedmon data.
class _ZedmonRecord {
  /// Record timestamp, relative to the time the zedmon device started.
  Duration timestamp;

  /// Measured power (Watts).
  double power;

  /// Parses a [_ZedmonRecord] from a line of zedmon's CSV output.
  _ZedmonRecord(String csvLine) {
    final parts = csvLine.split(',');
    if (parts.length != 4) {
      throw ZedmonException(
          'Zedmon CSV line does not have 4 entries. Offending line:\n$csvLine');
    }
    timestamp = Duration(microseconds: int.parse(parts.first));
    power = double.parse(parts.last);
  }
}

class _ZedmonProcess {
  /// Path to the zedmon client binary.
  final String _executablePath;

  /// Process executing the zedmon client.
  Process _process;

  /// First timestamp of a zedmon record.
  Duration _firstTimestamp;

  /// The previously-parsed zedmon record.
  _ZedmonRecord _prevRecord;

  /// Aggregated energy use (Joules) over the course of the trace.
  double _totalEnergy;

  /// Whether [_process] is being killed. Prevents further data processing.
  bool _stopped;

  _ZedmonProcess(this._executablePath) {
    _totalEnergy = 0.0;
    _stopped = false;
  }

  /// Starts the zedmon client process.
  Future<void> start() async {
    _process = await Process.start(_executablePath, ['record', '-out', '-']);
    _process.stdout
        .transform(utf8.decoder)
        .transform(LineSplitter())
        .listen(_parseStreamData);
  }

  /// Stops the zedmon client process and returns a summarized power
  /// measurement.
  //
  // TODO(fxbug.dev/44358): Implement downsampling to Zedmon client, and then provide
  // access to the downsampled timeseries instead of just an average.
  Future<PowerSummary> stop() async {
    _stopped = true;
    _process.kill(ProcessSignal.sigint);
    await _process.stderr.drain();

    final duration = _prevRecord.timestamp - _firstTimestamp;
    return PowerSummary(
        _totalEnergy / (duration.inMicroseconds * 1e-6), duration);
  }

  /// Parses a chunk of data from zedmon's CSV-formatted output stream.
  void _parseStreamData(String line) {
    if (_stopped) {
      return;
    }

    final record = _ZedmonRecord(line);
    if (_firstTimestamp == null) {
      _firstTimestamp = record.timestamp;
      _prevRecord = record;
      return;
    }

    if (record.timestamp <= _prevRecord.timestamp) {
      throw ZedmonException(
          'Zedmon record timestamps must be strictly increasing. '
          '(Found ${record.timestamp} after ${_prevRecord.timestamp}.)');
    }
    _totalEnergy += _prevRecord.power *
        (record.timestamp - _prevRecord.timestamp).inMicroseconds *
        1e-6;
    _prevRecord = record;
  }
}

/// Interface between the Zedmon power-measurement device and Catapult.
///
/// Zedmon is a device for measuring power consumption designed for use within
/// the Fuchsia project. Its schematic and source code are located at
/// https://fuchsia.googlesource.com/zedmon.
///
/// Prior to using this interface, you must have a zedmon device with up-to-date
/// firmware spliced into the power connection of your device-under-test and
/// connected to your host machine via USB. This class interfaces with zedmon
/// via its client binary, which converts zedmon's raw USB outputs into CSV
/// form.
///
/// Sample usage:
///   final power = Power('path/to/zedmon/client', dump, performance);
///   await power.startRecording();
///   ...do interesting things that consume power...
///   final file = await power.stopRecording('path/to/catapult_converter');
/// Then `file` contains power data formmatted for consumption by Catapult.
class Power {
  final String _zedmonExecutablePath;
  final Dump _dump;
  final Performance _performance;
  _ZedmonProcess _zedmon;

  /// Provides access to the last timestamp seen by [_zedmon] to allow
  /// synchronization in tests.
  @visibleForTesting
  Duration zedmonLatestTimestamp() {
    return _zedmon?._prevRecord?.timestamp;
  }

  Power(this._zedmonExecutablePath, this._dump, this._performance);

  /// Start collecting power data.
  Future<void> startRecording() async {
    _zedmon = _ZedmonProcess(_zedmonExecutablePath);
    await _zedmon.start();
  }

  /// Stop collecting power data.
  Future<void> stopRecording(String converterPath) async {
    final powerSummary = await _zedmon.stop();
    _zedmon = null;
    final testCaseResults =
        TestCaseResults('power', Unit.watts, [powerSummary.averagePower]);
    final List<Map<String, dynamic>> results = [
      testCaseResults.toJson(testSuite: 'fuchsia.power.assistant_power')
    ];
    final perfFile = await _dump.writeAsString(
        'power', 'fuchsiaperf.json', json.encode(results));

    await _performance.convertResults(
        converterPath, perfFile, Platform.environment);
  }
}

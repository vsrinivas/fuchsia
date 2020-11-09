// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';
import 'dart:convert' show jsonDecode, LineSplitter, utf8;
import 'dart:core';
import 'dart:io' show Directory, File, Platform, Process, sleep;

import 'package:test/test.dart';

class ZedmonException implements Exception {
  final String message;

  ZedmonException(this.message);

  @override
  String toString() => 'ZedmonException: $message';
}

// Description of the Zedmon device and host-side client.
class ZedmonDescription {
  final double shuntResistance;
  final int timestampIndex;
  final int shuntVoltageIndex;
  final int busVoltageIndex;
  final int powerIndex;

  ZedmonDescription(this.shuntResistance, this.timestampIndex,
      this.shuntVoltageIndex, this.busVoltageIndex, this.powerIndex);
}

// Uses `zedmon describe` to create a ZedmonDescription.
Future<ZedmonDescription> getZedmonDescription(String zedmonPath) async {
  final result = await Process.run(zedmonPath, ['describe']);
  final description = jsonDecode(result.stdout);

  final csvFormat = description['csv_header'].split(',');
  for (var field in [
    'timestamp_micros',
    'shunt_voltage',
    'bus_voltage',
    'power'
  ]) {
    if (!csvFormat.contains(field)) {
      throw ZedmonException('CSV header does not contain field "$field".');
    }
  }

  return ZedmonDescription(
      description['shunt_resistance'],
      csvFormat.indexOf('timestamp_micros'),
      csvFormat.indexOf('shunt_voltage'),
      csvFormat.indexOf('bus_voltage'),
      csvFormat.indexOf('power'));
}

void validateZedmonCsvLine(String csvLine, ZedmonDescription desc) {
  final parts = csvLine.split(',');
  final shuntVoltage = double.parse(parts[desc.shuntVoltageIndex]);
  final busVoltage = double.parse(parts[desc.busVoltageIndex]);
  final power = double.parse(parts[desc.powerIndex]);

  expect(
      busVoltage * shuntVoltage / desc.shuntResistance, closeTo(power, 1e-4));
}

// Returns the average power measured by `zedmon record` over the specified
// number of seconds.
Future<double> measureAveragePower(String zedmonPath, String tempFilePath,
    ZedmonDescription desc, int seconds) async {
  final result = await Process.run(zedmonPath,
      ['record', '--out', tempFilePath, '--duration', '${seconds}s']);
  expect(result.exitCode, 0);

  bool initialized = false;

  int firstTimestampMicros = 0;
  int prevTimestampMicros = 0;
  double prevPower = 0.0;
  double totalEnergy = 0.0;

  await for (String line in File(tempFilePath)
      .openRead()
      .transform(utf8.decoder)
      .transform(LineSplitter())) {
    final parts = line.split(',');
    final timestampMicros = int.parse(parts[desc.timestampIndex]);
    final power = double.parse(parts[desc.powerIndex]);

    if (initialized) {
      final dt = timestampMicros - prevTimestampMicros;

      // Use the trapezoid rule to estimate energy consumed since the previous
      // sample.
      totalEnergy += dt * (power + prevPower) / 2.0;

      prevTimestampMicros = timestampMicros;
      prevPower = power;
    } else {
      initialized = true;
      firstTimestampMicros = timestampMicros;
    }

    prevTimestampMicros = timestampMicros;
    prevPower = power;
  }
  return totalEnergy / (prevTimestampMicros - firstTimestampMicros);
}

// In order to run these tests, the host should be connected to exactly one
// Zedmon device, satisfying:
//  - Hardware version 2.1 (version is printed on the board);
//  - Firmware built from the Zedmon repository's revision cdc9458f45, or
//    equivalent.
//
// The Zedmon must be connected to a test device that:
//  - Consumes a nontrivial amount of power (>1W will certainly suffice);
//  - Consumes nontrial power within 1 second of being connected to power;
//
// Zedmon's relay must be on (its default state) at the beginning of the test.
// The test device will be power-cycled in the course of testing.
Future<void> main() async {
  String zedmonPath;
  ZedmonDescription zedmonDescription;
  Directory tempDir;
  String tempFilePath;

  setUpAll(() async {
    zedmonPath = Platform.script.resolve('runtime_deps/zedmon').toFilePath();
    zedmonDescription = await getZedmonDescription(zedmonPath);
    tempDir = await Directory.current.createTemp();
    tempFilePath = '${tempDir.path}/zedmon.csv';
  });

  tearDown(() async {
    final file = File(tempFilePath);
    if (file.existsSync()) {
      file.deleteSync();
    }
  });

  // `zedmon list` should yield exactly one word, containing a serial number.
  test('zedmon list', () async {
    final result = await Process.run(zedmonPath, ['list']);
    expect(result.exitCode, 0);

    final regex = RegExp(r'\W+');
    expect(regex.allMatches(result.stdout).length, 1);
  });

  // Records 1 second of Zedmon data and validates the power calculation for
  // each line of output.
  test('zedmon record', () async {
    final result = await Process.run(
        zedmonPath, ['record', '--out', tempFilePath, '--duration', '1s']);
    expect(result.exitCode, 0);

    var csvLinesRead = 0;
    await for (String line in File(tempFilePath)
        .openRead()
        .transform(utf8.decoder)
        .transform(LineSplitter())) {
      validateZedmonCsvLine(line, zedmonDescription);
      csvLinesRead += 1;
    }

    expect(csvLinesRead, greaterThan(1000));
  });

  // Tests that the 5-second average power drops by at least 99% when the
  // relay is turned off.
  test('zedmon relay', () async {
    var result = await Process.run(zedmonPath, ['relay', 'off']);
    expect(result.exitCode, 0);
    sleep(Duration(seconds: 1));
    final offPower = await measureAveragePower(
        zedmonPath, tempFilePath, zedmonDescription, 5);

    result = await Process.run(zedmonPath, ['relay', 'on']);
    expect(result.exitCode, 0);
    sleep(Duration(seconds: 1));
    final onPower = await measureAveragePower(
        zedmonPath, tempFilePath, zedmonDescription, 5);

    expect(offPower, lessThan(0.01 * onPower));
  });
}

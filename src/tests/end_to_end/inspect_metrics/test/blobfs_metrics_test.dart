// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Tests that blobfs exposes valid metrics via inspect

import 'package:test/test.dart';
import 'package:sl4f/sl4f.dart' as sl4f;
import 'util.dart';

const String fshostPath = 'bootstrap/fshost:root';
const String pagedPath = '$fshostPath/paged_read_stats/*';
const String unpagedPath = '$fshostPath/unpaged_read_stats/*';

Future<int> sumOfProperties(sl4f.Inspect inspect, String path) async {
  final properties = await getInspectValues(inspect, path);
  return properties.reduce((a, b) => a + b);
}

void main() {
  sl4f.Sl4f sl4fDriver;
  sl4f.Inspect inspect;

  setUp(() async {
    sl4fDriver = sl4f.Sl4f.fromEnvironment();
    await sl4fDriver.startServer();
    inspect = sl4f.Inspect(sl4fDriver);
  });

  tearDown(() async {
    await sl4fDriver.stopServer();
    sl4fDriver.close();
  });

  tearDownAll(printErrorHelp);

  withLongTimeout(() {
    test('BlobFS exposes lookup statistics', () async {
      expect(
          await getInspectValues(
              inspect, '$fshostPath/lookup_stats:blobs_opened'),
          singleValue(greaterThan(0)));
      expect(
          await getInspectValues(
              inspect, '$fshostPath/lookup_stats:blobs_opened_total_size'),
          singleValue(greaterThan(0)));
    });

    test('BlobFS exposes allocation statistics', () async {
      expect(
          await getInspectValues(
              inspect, '$fshostPath/allocation_stats:blobs_created'),
          singleValue(greaterThanOrEqualTo(0)));
      expect(
          await getInspectValues(inspect,
              '$fshostPath/allocation_stats:total_allocation_time_ticks'),
          singleValue(greaterThanOrEqualTo(0)));
    });

    test('BlobFS exposes writeback statistics', () async {
      expect(
          await getInspectValues(
              inspect, '$fshostPath/writeback_stats:data_bytes_written'),
          singleValue(greaterThanOrEqualTo(0)));
      expect(
          await getInspectValues(
              inspect, '$fshostPath/writeback_stats:merkle_bytes_written'),
          singleValue(greaterThanOrEqualTo(0)));
      expect(
          await getInspectValues(inspect,
              '$fshostPath/writeback_stats:total_merkle_generation_time_ticks'),
          singleValue(greaterThanOrEqualTo(0)));
      expect(
          await getInspectValues(inspect,
              '$fshostPath/writeback_stats:total_write_enqueue_time_ticks'),
          singleValue(greaterThanOrEqualTo(0)));
    });

    test('BlobFS exposes read statistics', () async {
      final pagedBytesRead =
          await sumOfProperties(inspect, '$pagedPath:read_bytes');
      final unpagedBytesRead =
          await sumOfProperties(inspect, '$unpagedPath:read_bytes');
      expect(pagedBytesRead + unpagedBytesRead, greaterThan(0));

      final pagedReadTicks =
          await sumOfProperties(inspect, '$pagedPath:read_ticks');
      final unpagedReadTicks =
          await sumOfProperties(inspect, '$unpagedPath:read_ticks');
      expect(pagedReadTicks + unpagedReadTicks, greaterThan(0));
    });
  });
}

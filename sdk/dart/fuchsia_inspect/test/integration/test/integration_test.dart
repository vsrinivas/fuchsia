// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// @dart = 2.9

import 'dart:convert';
import 'package:fidl_fuchsia_component/fidl_async.dart' as fcomponent;
import 'package:fidl_fuchsia_component_decl/fidl_async.dart' as fdecl;
import 'package:fidl_fuchsia_diagnostics/fidl_async.dart' as fdiagnostics;
import 'package:fidl_fuchsia_io/fidl_async.dart' as fio;
import 'package:fuchsia_inspect/reader.dart';
import 'package:fuchsia_services/services.dart' as services;
import 'package:test/test.dart';

const testComponentName = 'writer';
const testComponentUrl = '#meta/$testComponentName.cm';

void main() async {
  final realm = fcomponent.RealmProxy();
  final reader = ArchiveReader.forInspect();
  final writerExposedDir = fio.DirectoryProxy();
  final writerBinder = fcomponent.BinderProxy();

  Future<DiagnosticsData<InspectMetadata>> takeSnapshot(String filename) async {
    final snapshot = await reader.snapshot(
        acceptSnapshot: (snapshot) =>
            // The writer exposes two vmo files, so we expect two snapshots.
            snapshot.length >= 2 &&
            _monikerList(snapshot)
                    .where((moniker) => moniker == 'writer')
                    .length ==
                2);
    expect(snapshot.length, greaterThanOrEqualTo(2));
    for (final data in snapshot) {
      if (data.metadata.filename.contains(filename)) {
        return data;
      }
    }
    return null;
  }

  ;

  setUpAll(() {
    services.Incoming.fromSvcPath()..connectToService(realm);
    final childRef = fdecl.ChildRef(
      name: 'writer',
      collection: null,
    );
    realm.openExposedDir(childRef, writerExposedDir.ctrl.request());
    services.Incoming.withDirectory(writerExposedDir)
        .connectToService(writerBinder);
  });

  tearDownAll(() {
    writerBinder.ctrl.close();
    writerExposedDir.ctrl.close();
    realm.ctrl.close();
  });

  test('read hierarchy', () async {
    final data = await takeSnapshot('root');
    expect(data, isNot(null));
    expect(data.payload['root']['t1']['version'], '1.0');
    expect(data.payload['root']['t1']['frame'], 'b64:AAAA');
    expect(data.payload['root']['t1']['value'], -10);
    expect(data.payload['root']['t1']['active'], true);
    expect(data.payload['root']['t1']['item-0x0']['value'], 10);
    expect(data.payload['root']['t1']['item-0x1']['value'], 100);
    expect(data.payload['root']['t2']['version'], '1.0');
    expect(data.payload['root']['t2']['frame'], 'b64:AAAA');
    expect(data.payload['root']['t2']['value'], -10);
    expect(data.payload['root']['t2']['active'], true);
    expect(data.payload['root']['t2']['item-0x2']['value'], 4);
  });

  test('dynamic generates new hierarchy', () async {
    final data = await takeSnapshot('digits_of_numbers');
    final data2 = await takeSnapshot('digits_of_numbers');

    expect(data, isNot(null));
    expect(data2, isNot(null));

    final dataIncrementValue =
        int.parse(data.payload['root']['increments']['value']);
    final dataDoubleValue = int.parse(data.payload['root']['doubles']['value']);
    final data2IncrementValue =
        int.parse(data2.payload['root']['increments']['value']);
    final data2DoubleValue =
        int.parse(data2.payload['root']['doubles']['value']);

    expect(data2IncrementValue, dataIncrementValue + 1);
    expect(data2DoubleValue, dataDoubleValue + 2);
  });
}

List<String> _monikerList(List<DiagnosticsData> diagnosticsDataList) =>
    diagnosticsDataList.map((e) => e.moniker).toList();

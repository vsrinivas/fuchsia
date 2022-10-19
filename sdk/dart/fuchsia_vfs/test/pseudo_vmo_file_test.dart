// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';
import 'dart:typed_data';
import 'dart:convert' show utf8;

import 'package:fidl/fidl.dart';
import 'package:fidl_fuchsia_io/fidl_async.dart';
import 'package:fuchsia_vfs/vfs.dart';
import 'package:test/test.dart';
import 'package:zircon/zircon.dart';

void main() {
  Future<void> _assertRead(FileProxy proxy) async {
    const bytesToTryCount = 16;
    const bytesExpectedCount = 0;
    final data = await proxy.read(bytesToTryCount);
    expect(data.length, bytesExpectedCount);
  }

  Future<void> _assertDescribeFile(FileProxy proxy) async {
    var response = await proxy.query();
    expect(utf8.decode(response), fileProtocolName);
  }

  Future<void> _assertDescribeVmo(
      FileProxy proxy, String expectedString) async {
    await _assertDescribeFile(proxy);

    final vmo = await proxy.getBackingMemory(VmoFlags.$none);
    expect(vmo.isValid, isTrue);
    final Uint8List vmoData = vmo.map();
    expect(String.fromCharCodes(vmoData.getRange(0, expectedString.length)),
        expectedString);
  }

  group('pseudo vmo file:', () {
    test('onOpen event on success', () async {
      final stringList = ['test string'];
      final file = _TestPseudoVmoFile.fromStringList(stringList);
      final proxy = file.connect(OpenFlags.rightReadable | OpenFlags.describe);

      await proxy.onOpen.first.then((response) {
        expect(response.s, ZX.OK);
        expect(response.info, isNotNull);
      }).catchError((err) async {
        fail(err.toString());
      });
    });

    test('read file', () async {
      final stringList = ['test string'];
      final file = _TestPseudoVmoFile.fromStringList(stringList);
      final proxy = file.connect(OpenFlags.rightReadable);
      await _assertRead(proxy);
    });

    test('describe file', () async {
      final stringList = ['test string', 'hello world', 'lorem ipsum'];
      final file = _TestPseudoVmoFile.fromStringList(stringList);

      for (final expectedString in stringList) {
        final proxy = file.connect(OpenFlags.rightReadable);
        await _assertDescribeVmo(proxy, expectedString);
        await proxy.close();
      }
    });
  });
}

class _TestPseudoVmoFile {
  _TestPseudoVmoFile._internal(this._pseudoVmoFile);

  factory _TestPseudoVmoFile.fromStringList(List<String> expectedStrings) {
    return _TestPseudoVmoFile._internal(
        PseudoVmoFile.readOnly(_vmoFromStringFactory(expectedStrings)));
  }

  factory _TestPseudoVmoFile.fromVmoFunc(VmoFn vmoFn) {
    return _TestPseudoVmoFile._internal(PseudoVmoFile.readOnly(vmoFn));
  }

  final PseudoVmoFile _pseudoVmoFile;

  static VmoFn _vmoFromStringFactory(List<String> expectedStrings) {
    int i = 0;

    // callback returns next string in list with each call, restarting at top of
    // list when out of strings
    return () {
      final SizedVmo sizedVmo = SizedVmo.fromUint8List(
          Uint8List.fromList(expectedStrings[i++].codeUnits));
      i %= expectedStrings.length;
      return sizedVmo;
    };
  }

  FileProxy connect(OpenFlags openRights) {
    final proxy = FileProxy();
    final channel = proxy.ctrl.request().passChannel();
    final interfaceRequest = InterfaceRequest<Node>(channel);
    _pseudoVmoFile.connect(openRights, modeTypeFile, interfaceRequest);
    return proxy;
  }
}

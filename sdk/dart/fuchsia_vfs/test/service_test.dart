// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';

import 'package:fidl/fidl.dart' hide Service;
import 'package:fidl_test_placeholders/fidl_async.dart';
import 'package:fidl_fuchsia_io/fidl_async.dart' as io_fidl;
import 'package:fuchsia_vfs/vfs.dart';
import 'package:test/test.dart';
import 'package:zircon/zircon.dart';

void main() {
  var unsupportedFlags = [
    io_fidl.OpenFlags.describe,
    io_fidl.OpenFlags.create,
    io_fidl.OpenFlags.createIfAbsent,
    io_fidl.OpenFlags.directory,
    io_fidl.OpenFlags.truncate,
    io_fidl.OpenFlags.noRemote,
    io_fidl.OpenFlags.nodeReference,
  ];

  group('service tests:', () {
    test('connect to service fails for bad flags', () async {
      for (var unsupportedFlag in unsupportedFlags) {
        var fs = _FsWithEchoService();
        EchoProxy echoProxy = EchoProxy();
        await fs.dirProxy.open(unsupportedFlag, 0, Echo.$serviceName,
            InterfaceRequest(echoProxy.ctrl.request().passChannel()));

        // as we passed invalid flag, other side will fail to connect and close
        // our request channel, test that.
        echoProxy.ctrl.whenClosed.asStream().listen(expectAsync1((_) {}));
      }
    });

    test('connect to service fails for bad flags and check status', () async {
      for (var unsupportedFlag in unsupportedFlags) {
        _EchoImpl echo = _EchoImpl();
        Service<Echo> service = Service.withConnector(echo.bind);
        EchoProxy echoProxy = EchoProxy();
        var expectedStatus = ZX.ERR_NOT_SUPPORTED;
        if (unsupportedFlag == io_fidl.OpenFlags.directory) {
          expectedStatus = ZX.ERR_NOT_DIR;
        }
        expect(
            service.connect(unsupportedFlag, 0,
                InterfaceRequest(echoProxy.ctrl.request().passChannel())),
            expectedStatus);
      }
    });

    test('connect to service fails for OpenFlags.describe', () async {
      var fs = _FsWithEchoService();

      var echoProxy = io_fidl.NodeProxy();
      await fs.dirProxy.open(io_fidl.OpenFlags.describe, 0, Echo.$serviceName,
          echoProxy.ctrl.request());
      echoProxy.onOpen.listen(expectAsync1((response) {
        expect(response.s, ZX.ERR_NOT_SUPPORTED);
        expect(response.info, isNull);
      }));
    });

    test('connect to service fails for invalid modes', () async {
      var fs = _FsWithEchoService();
      var invalidModes = [
        io_fidl.modeTypeBlockDevice,
        io_fidl.modeTypeDirectory,
        io_fidl.modeTypeFile,
        io_fidl.modeTypeMask,
        io_fidl.modeTypeSocket
      ];
      for (var mode in invalidModes) {
        var echoProxy = io_fidl.NodeProxy();
        await fs.dirProxy.open(io_fidl.OpenFlags.describe, mode,
            Echo.$serviceName, echoProxy.ctrl.request());

        echoProxy.onOpen.listen(expectAsync1((response) {
          expect(response.s, ZX.ERR_INVALID_ARGS);
          expect(response.info, isNull);
        }));
      }
    });

    test('connect to service fails for OpenFlags.directory', () async {
      var fs = _FsWithEchoService();
      var nodeProxy = io_fidl.NodeProxy();
      await fs.dirProxy.open(
          io_fidl.OpenFlags.directory | io_fidl.OpenFlags.describe,
          0,
          Echo.$serviceName,
          nodeProxy.ctrl.request());

      nodeProxy.onOpen.listen(expectAsync1((response) {
        expect(response.s, ZX.ERR_NOT_DIR);
        expect(response.info, isNull);
      }));
    });

    test('connect to service passes with valid flags', () async {
      var supportedFlags = [
        io_fidl.OpenFlags.rightReadable,
        io_fidl.OpenFlags.rightWritable
      ];
      for (var supportedFlag in supportedFlags) {
        var fs = _FsWithEchoService();
        // connect to service
        var echoProxy = EchoProxy();

        await fs.dirProxy.open(supportedFlag, 0, Echo.$serviceName,
            InterfaceRequest(echoProxy.ctrl.request().passChannel()));
        String str = 'my message';
        var got = await echoProxy.echoString(str);
        expect(got, str);
      }
    });

    test('connect to service passes with valid modes', () async {
      var supportedModes = [
        io_fidl.modeProtectionMask,
        io_fidl.modeTypeService
      ];
      for (var supportedMode in supportedModes) {
        var fs = _FsWithEchoService();
        // connect to service
        var echoProxy = EchoProxy();

        await fs.dirProxy.open(
            io_fidl.OpenFlags.rightReadable,
            supportedMode,
            Echo.$serviceName,
            InterfaceRequest(echoProxy.ctrl.request().passChannel()));
        String str = 'my message';
        var got = await echoProxy.echoString(str);
        expect(got, str);
      }
    });
  });
}

class _FsWithEchoService {
  final io_fidl.DirectoryProxy dirProxy = io_fidl.DirectoryProxy();
  final PseudoDir _dir = PseudoDir();
  _EchoImpl echo = _EchoImpl();

  _FsWithEchoService() {
    Service<Echo> service = Service.withConnector(echo.bind);
    var status = _dir.connect(
        io_fidl.OpenFlags.rightReadable | io_fidl.OpenFlags.rightWritable,
        0,
        InterfaceRequest(dirProxy.ctrl.request().passChannel()));
    expect(status, ZX.OK);
    _dir.addNode(Echo.$serviceName, service);
  }
}

class _EchoImpl extends Echo {
  final EchoBinding _binding = EchoBinding();

  void bind(InterfaceRequest<Echo> request) {
    _binding.bind(this, request);
  }

  @override
  Future<String?> echoString(String? value) async {
    return value;
  }
}

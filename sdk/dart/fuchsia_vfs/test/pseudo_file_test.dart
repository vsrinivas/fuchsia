// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';
import 'dart:typed_data';

import 'package:fidl/fidl.dart';
import 'package:fidl_fuchsia_io/fidl_async.dart';
import 'package:fuchsia_vfs/vfs.dart';
import 'package:test/test.dart';
import 'package:zircon/zircon.dart';

void main() {
  const _newStr = 'new_str';
  final _newStrList = Uint8List.fromList(_newStr.codeUnits);

  InterfaceRequest<Node> _getNodeInterfaceRequest(FileProxy proxy) {
    return InterfaceRequest<Node>(proxy.ctrl.request().passChannel());
  }

  _ReadOnlyFile _createReadOnlyFile(String str, OpenFlags openFlags,
      [int expectedStatus = ZX.OK]) {
    _ReadOnlyFile file = _ReadOnlyFile()
      ..pseudoFile = PseudoFile.readOnlyStr(() {
        return str;
      })
      ..proxy = FileProxy();
    expect(
        file.pseudoFile
            .connect(openFlags, 0, _getNodeInterfaceRequest(file.proxy)),
        expectedStatus);
    return file;
  }

  Future<void> _assertFinalBuffer(FileProxy proxy, _ReadWriteFile file,
      String oldStr, String newStr) async {
    // our buffer should contain old string
    expect(file.buffer, oldStr);

    await proxy.close();

    // our buffer should contain new string
    expect(file.buffer, newStr);
  }

  Future<void> _assertWrite(FileProxy proxy, Uint8List content,
      {int expectedStatus = ZX.OK, int? expectedSize}) async {
    expectedSize ??= content.length;
    if (expectedStatus == ZX.OK) {
      final actualCount = await proxy.write(content);
      expect(actualCount, expectedSize);
    } else {
      await expectLater(
          proxy.write(content),
          throwsA(isA<MethodException>()
              .having((e) => e.value, 'value', equals(expectedStatus))));
    }
  }

  Future<void> _assertRead(FileProxy proxy, int bufSize, String expectedStr,
      {expectedStatus = ZX.OK}) async {
    if (expectedStatus == ZX.OK) {
      final data = await proxy.read(bufSize);
      expect(String.fromCharCodes(data), expectedStr);
    } else {
      await expectLater(
          proxy.read(bufSize),
          throwsA(isA<MethodException>()
              .having((e) => e.value, 'value', equals(expectedStatus))));
    }
  }

  Future<void> _assertReadAt(
      FileProxy proxy, int bufSize, int offset, String expectedStr,
      {expectedStatus = ZX.OK}) async {
    if (expectedStatus == ZX.OK) {
      final data = await proxy.readAt(bufSize, offset);
      expect(String.fromCharCodes(data), expectedStr);
    } else {
      await expectLater(
          proxy.readAt(bufSize, offset),
          throwsA(isA<MethodException>()
              .having((e) => e.value, 'value', equals(expectedStatus))));
    }
  }

  Future<void> _assertWriteAt(
      FileProxy proxy, Uint8List content, int offset, int expectedWrittenLen,
      {expectedStatus = ZX.OK}) async {
    if (expectedStatus == ZX.OK) {
      final actualCount = await proxy.writeAt(content, offset);
      expect(actualCount, expectedWrittenLen);
    } else {
      await expectLater(
          proxy.writeAt(content, offset),
          throwsA(isA<MethodException>()
              .having((e) => e.value, 'value', equals(expectedStatus))));
    }
  }

  group('pseudo file creation validation: ', () {
    PseudoFile _createReadWriteFileStub() {
      return PseudoFile.readWriteStr(1, () {
        return '';
      }, (String str) {
        return ZX.OK;
      });
    }

    final _notAllowedFlags = [
      OpenFlags.create,
      OpenFlags.createIfAbsent,
      OpenFlags.noRemote,
    ];

    test('onOpen event on flag validation error', () async {
      final file = _createReadOnlyFile('',
          OpenFlags.rightWritable | OpenFlags.describe, ZX.ERR_NOT_SUPPORTED);

      await file.proxy.onOpen.first.then((response) {
        expect(response.s, ZX.ERR_NOT_SUPPORTED);
        expect(response.info, isNull);
      }).catchError((err) async {
        fail(err.toString());
      });
    });

    test('read only file', () async {
      final file = _createReadOnlyFile(
          '', OpenFlags.rightWritable, ZX.ERR_NOT_SUPPORTED);

      {
        final proxy = FileProxy();
        expect(
            file.pseudoFile.connect(
                OpenFlags.truncate, 0, _getNodeInterfaceRequest(proxy)),
            ZX.ERR_NOT_SUPPORTED);
      }

      {
        final proxy = FileProxy();
        expect(
            file.pseudoFile.connect(
                OpenFlags.directory, 0, _getNodeInterfaceRequest(proxy)),
            ZX.ERR_NOT_DIR);
      }

      {
        final proxy = FileProxy();
        expect(
            file.pseudoFile
                .connect(OpenFlags.append, 0, _getNodeInterfaceRequest(proxy)),
            ZX.ERR_INVALID_ARGS);
      }

      for (final flag in _notAllowedFlags) {
        final proxy = FileProxy();
        expect(
            file.pseudoFile.connect(flag, 0, _getNodeInterfaceRequest(proxy)),
            ZX.ERR_NOT_SUPPORTED,
            reason: 'for flag: $flag');
      }
    });

    test('read write file', () async {
      final file = _createReadWriteFileStub();

      {
        final proxy = FileProxy();
        expect(
            file.connect(
                OpenFlags.directory, 0, _getNodeInterfaceRequest(proxy)),
            ZX.ERR_NOT_DIR);
      }

      {
        final proxy = FileProxy();
        expect(
            file.connect(OpenFlags.append, 0, _getNodeInterfaceRequest(proxy)),
            ZX.ERR_INVALID_ARGS);
      }

      for (final flag in _notAllowedFlags) {
        final proxy = FileProxy();
        expect(file.connect(flag, 0, _getNodeInterfaceRequest(proxy)),
            ZX.ERR_NOT_SUPPORTED,
            reason: 'for flag: $flag');
      }
    });

    test('connect file with mode', () async {
      final file = _createReadWriteFileStub();

      {
        final proxy = FileProxy();
        expect(
            file.connect(OpenFlags.rightReadable | OpenFlags.describe,
                ~modeTypeFile, _getNodeInterfaceRequest(proxy)),
            ZX.ERR_INVALID_ARGS);

        await proxy.onOpen.first.then((response) {
          expect(response.s, ZX.ERR_INVALID_ARGS);
          expect(response.info, isNull);
        }).catchError((err) async {
          fail(err.toString());
        });
      }

      {
        final proxy = FileProxy();
        expect(
            file.connect(OpenFlags.rightReadable | OpenFlags.describe,
                modeTypeFile, _getNodeInterfaceRequest(proxy)),
            ZX.OK);
        await proxy.onOpen.first.then((response) {
          expect(response.s, ZX.OK);
          expect(response.info, isNotNull);
        }).catchError((err) async {
          fail(err.toString());
        });
      }
    });

    test('open fails', () async {
      final file = _createReadWriteFileStub();

      final paths = ['', '/', '.', './', './/', './//'];
      for (final path in paths) {
        final proxy = FileProxy();
        file.open(OpenFlags.rightReadable | OpenFlags.describe, 0, path,
            _getNodeInterfaceRequest(proxy), OpenFlags.rightReadable);

        await proxy.onOpen.first.then((response) {
          expect(response.s, ZX.ERR_NOT_DIR);
          expect(response.info, isNull);
        }).catchError((err) async {
          fail(err.toString());
        });
      }
    });
  });

  group('pseudo file:', () {
    _ReadWriteFile _createReadWriteFile(String initialStr,
        {int? capacity, createProxy = true, OpenFlags? flags}) {
      int c = initialStr.length;
      if (capacity != null) {
        assert(capacity >= initialStr.length);
        c = capacity;
      }
      _ReadWriteFile file = _ReadWriteFile();
      file
        ..buffer = initialStr
        ..pseudoFile = PseudoFile.readWriteStr(c, () {
          return file.buffer;
        }, (String str) {
          file.buffer = str;
          return ZX.OK;
        });
      if (createProxy) {
        file.proxy = FileProxy();
        expect(
            file.pseudoFile.connect(
                flags ?? OpenFlags.rightReadable | OpenFlags.rightWritable,
                0,
                _getNodeInterfaceRequest(file.proxy)),
            ZX.OK);
      }
      return file;
    }

    test('onOpen event on success', () async {
      final file = _createReadOnlyFile(
          'test_str', OpenFlags.rightReadable | OpenFlags.describe);

      await file.proxy.onOpen.first.then((response) {
        expect(response.s, ZX.OK);
        expect(response.info, isNotNull);
      }).catchError((err) async {
        fail(err.toString());
      });
    });

    final expectedNodeAttrs = NodeAttributes(
        mode: modeTypeFile | modeProtectionMask,
        id: inoUnknown,
        contentSize: 0,
        storageSize: 0,
        linkCount: 1,
        creationTime: 0,
        modificationTime: 0);

    test('test getAttr', () async {
      final file = _createReadOnlyFile('test_str', OpenFlags.rightReadable);
      final response = await file.proxy.getAttr();
      expect(response.s, ZX.OK);
      expect(response.attributes, expectedNodeAttrs);
    });

    test('clone works', () async {
      final file = _createReadOnlyFile('test_str', OpenFlags.rightReadable);

      final clonedProxy = FileProxy();
      await file.proxy.clone(OpenFlags.rightReadable | OpenFlags.describe,
          _getNodeInterfaceRequest(clonedProxy));

      await clonedProxy.onOpen.first.then((response) {
        expect(response.s, ZX.OK);
        expect(response.info, isNotNull);
      }).catchError((err) async {
        fail(err.toString());
      });
    });

    test('clone works with POSIX compatibility flags', () async {
      final file = _createReadOnlyFile('test_str', OpenFlags.rightReadable);

      final clonedProxy = FileProxy();
      await file.proxy.clone(
          OpenFlags.rightReadable |
              OpenFlags.describe |
              OpenFlags.posixWritable |
              OpenFlags.posixExecutable,
          _getNodeInterfaceRequest(clonedProxy));

      await clonedProxy.onOpen.first.then((response) {
        expect(response.s, ZX.OK);
        expect(response.info, isNotNull);
      }).catchError((err) async {
        fail(err.toString());
      });
    });

    test('clone fails when trying to pass Readable flag to Node Reference',
        () async {
      final file = _createReadOnlyFile(
          'test_str', OpenFlags.rightReadable | OpenFlags.nodeReference);

      final clonedProxy = FileProxy();
      await file.proxy.clone(OpenFlags.rightReadable | OpenFlags.describe,
          _getNodeInterfaceRequest(clonedProxy));

      await clonedProxy.onOpen.first.then((response) {
        expect(response.s, ZX.ERR_ACCESS_DENIED);
        expect(response.info, isNull);
      }).catchError((err) async {
        fail(err.toString());
      });
    });

    test('clone fails when trying to pass Writable flag to Node Reference',
        () async {
      final file = _createReadWriteFile('test_str',
          flags: OpenFlags.rightWritable | OpenFlags.nodeReference);

      final clonedProxy = FileProxy();
      await file.proxy.clone(OpenFlags.rightWritable | OpenFlags.describe,
          _getNodeInterfaceRequest(clonedProxy));

      await clonedProxy.onOpen.first.then((response) {
        expect(response.s, ZX.ERR_ACCESS_DENIED);
        expect(response.info, isNull);
      }).catchError((err) async {
        fail(err.toString());
      });
    });

    test('able to clone Node Reference', () async {
      final file = _createReadOnlyFile('test_str', OpenFlags.nodeReference);

      final clonedProxy = FileProxy();
      await file.proxy.clone(OpenFlags.nodeReference | OpenFlags.describe,
          _getNodeInterfaceRequest(clonedProxy));

      await clonedProxy.onOpen.first.then((response) {
        expect(response.s, ZX.OK);
        expect(response.info, isNotNull);
      }).catchError((err) async {
        fail(err.toString());
      });
    });

    test('clone should fail if requested rights exceed source rights',
        () async {
      final file = _createReadWriteFile('test_str', createProxy: false);
      final flagsToTest = [OpenFlags.rightReadable, OpenFlags.rightWritable];

      for (final flag in flagsToTest) {
        final proxy = FileProxy();
        expect(
            file.pseudoFile
                .connect(OpenFlags.$none, 0, _getNodeInterfaceRequest(proxy)),
            ZX.OK);

        final clonedProxy = FileProxy();
        await proxy.clone(
            flag | OpenFlags.describe, _getNodeInterfaceRequest(clonedProxy));

        await clonedProxy.onOpen.first.then((response) {
          expect(response.s, ZX.ERR_ACCESS_DENIED);
          expect(response.info, isNull);
        }).catchError((err) async {
          fail(err.toString());
        });
      }
    });

    test('onOpen with describe flag', () async {
      final file = _createReadOnlyFile(
          'test_str', OpenFlags.rightReadable | OpenFlags.describe);

      await file.proxy.onOpen.first.then((response) {
        expect(response.s, ZX.OK);
        expect(response.info, isNotNull);
      }).catchError((err) async {
        fail(err.toString());
      });
    });

    test('onOpen with NodeReference flag', () async {
      final file = _createReadOnlyFile(
          'test_str', OpenFlags.nodeReference | OpenFlags.describe);

      await file.proxy.onOpen.first.then((response) {
        expect(response.s, ZX.OK);
        expect(response.info, isNotNull);
      }).catchError((err) async {
        fail(err.toString());
      });
    });

    test('Directory not ignored with NodeReference flag', () async {
      final file = _createReadOnlyFile(
          'test_str',
          OpenFlags.nodeReference | OpenFlags.describe | OpenFlags.directory,
          ZX.ERR_NOT_DIR);

      await file.proxy.onOpen.first.then((response) {
        expect(response.s, ZX.ERR_NOT_DIR);
        expect(response.info, isNull);
      }).catchError((err) async {
        fail(err.toString());
      });
    });

    test('GetAttr with NodeReference flag', () async {
      final file = _createReadOnlyFile('test_str', OpenFlags.nodeReference);
      final response = await file.proxy.getAttr();
      expect(response.s, ZX.OK);
      expect(response.attributes, expectedNodeAttrs);
    });

    test('read file', () async {
      const str = 'test_str';
      final file = _createReadOnlyFile(str, OpenFlags.rightReadable);
      await _assertRead(file.proxy, 10, str);
    });

    test('read functions fails for NodeReference flag', () async {
      final file = _createReadOnlyFile(
          'test_str', OpenFlags.rightReadable | OpenFlags.nodeReference);
      await expectLater(
          file.proxy.read(1024),
          throwsA(isA<MethodException>()
              .having((e) => e.value, 'value', equals(ZX.ERR_ACCESS_DENIED))));

      await expectLater(
          file.proxy.readAt(0, 1024),
          throwsA(isA<MethodException>()
              .having((e) => e.value, 'value', equals(ZX.ERR_ACCESS_DENIED))));
    });

    Future<void> _resetSeek(FileProxy proxy, [int? offset]) async {
      // Reset seek.
      int _ = await proxy.seek(SeekOrigin.start, offset ?? 0);
    }

    test('simple write file', () async {
      const str = 'test_str';
      final file = _createReadWriteFile(str);
      final proxy = file.proxy;

      await _assertWrite(proxy, _newStrList);

      await _resetSeek(proxy);

      await _assertRead(file.proxy, 100, _newStr);

      await _assertFinalBuffer(file.proxy, file, str, _newStr);
    });

    test('readAt', () async {
      const str = 'test_str';
      final file = _createReadOnlyFile(str, OpenFlags.rightReadable);

      for (int i = 0; i < str.length; i++) {
        await _assertReadAt(file.proxy, 100, i, str.substring(i));
      }
    });

    test('read after readAt', () async {
      const str = 'test_str';
      final file = _createReadOnlyFile(str, OpenFlags.rightReadable);

      await _assertReadAt(file.proxy, 100, 1, str.substring(1));

      //readAt should not change seek
      await _assertRead(file.proxy, 100, str);
    });

    test('read should not affect readAt', () async {
      const str = 'test_str';
      final file = _createReadOnlyFile(str, OpenFlags.rightReadable);

      await _assertRead(file.proxy, 100, str);

      // after seek is changed, readAt should be able to read at any position
      await _assertReadAt(file.proxy, 100, 2, str.substring(2));
    });

    test('readAt should work for arbitrary length', () async {
      const str = 'test_str';
      final file = _createReadOnlyFile(str, OpenFlags.rightReadable);

      // try to read 3 chars
      await _assertReadAt(file.proxy, 3, 2, str.substring(2, 2 + 3));
    });

    test('read should work after readAt read arbitrary length', () async {
      const str = 'test_str';
      final file = _createReadOnlyFile(str, OpenFlags.rightReadable);

      await _assertReadAt(file.proxy, 3, 2, str.substring(2, 2 + 3));

      await _assertRead(file.proxy, 3, str.substring(0, 0 + 3));
    });

    test('readAt should fail for passing offset more than length of file',
        () async {
      const str = 'test_str';
      final file = _createReadOnlyFile(str, OpenFlags.rightReadable);

      await _assertReadAt(file.proxy, 100, str.length + 1, '',
          expectedStatus: ZX.ERR_OUT_OF_RANGE);
    });

    test('readAt should not fail for passing offset equal to length of file',
        () async {
      const str = 'test_str';
      final file = _createReadOnlyFile(str, OpenFlags.rightReadable);

      await _assertReadAt(file.proxy, 100, str.length, '');
    });

    test('read should not fail for reading from end of file', () async {
      const str = 'test_str';
      final file = _createReadOnlyFile(str, OpenFlags.rightReadable);

      await _resetSeek(file.proxy, str.length);
      await _assertRead(file.proxy, 100, '');
    });

    test('write file at arbitrary position', () async {
      const str = 'test_str';
      final file = _createReadWriteFile(str);
      final proxy = file.proxy;

      await _resetSeek(proxy, 1);

      await _assertWrite(proxy, _newStrList);

      await _resetSeek(proxy);

      final expectedStr = str.substring(0, 1) + _newStr;
      await _assertRead(proxy, 100, expectedStr);

      await _assertFinalBuffer(proxy, file, str, expectedStr);
    });

    test('write should truncate if not enough capacity', () async {
      const str = 'test_str';
      final file = _createReadWriteFile(str);
      final proxy = file.proxy;

      await _resetSeek(proxy, 2);
      await _assertWrite(proxy, _newStrList, expectedSize: _newStr.length - 1);
      await _resetSeek(proxy);

      final expectedStr =
          str.substring(0, 2) + _newStr.substring(0, _newStr.length - 1);
      await _assertRead(proxy, 100, expectedStr);

      await _assertFinalBuffer(proxy, file, str, expectedStr);
    });

    test('write at end should fail', () async {
      const str = 'test_str';
      final file = _createReadWriteFile(str);
      final proxy = file.proxy;

      await _resetSeek(proxy, str.length);
      await _assertWrite(proxy, _newStrList,
          expectedStatus: ZX.ERR_OUT_OF_RANGE, expectedSize: 0);

      await _resetSeek(proxy);

      await _assertRead(proxy, 100, str);

      await _assertFinalBuffer(proxy, file, str, str);
    });

    group('write file when capacity is not initial length:', () {
      test('should not resize if there is enough capacity', () async {
        const str = 'test_str';
        final file = _createReadWriteFile(str, capacity: str.length + 2);
        final proxy = file.proxy;

        await _resetSeek(proxy, 3);
        await _assertWrite(proxy, _newStrList);

        final expectedStr =
            str.substring(0, 3) + _newStr.substring(0, _newStr.length);
        await _assertReadAt(proxy, 100, 0, expectedStr);

        await _assertFinalBuffer(proxy, file, str, expectedStr);
      });

      test('write should fail when capacity is reached', () async {
        const str = 'test_str';
        final file = _createReadWriteFile(str, capacity: str.length + 2);
        final proxy = file.proxy;

        await _resetSeek(proxy, 3);
        await _assertWrite(proxy, _newStrList);

        final expectedStr =
            str.substring(0, 3) + _newStr.substring(0, _newStr.length);

        // write at end, should fail
        await _resetSeek(proxy, expectedStr.length);
        await _assertWrite(proxy, _newStrList,
            expectedStatus: ZX.ERR_OUT_OF_RANGE, expectedSize: 0);

        // no write should have happened
        await _assertReadAt(proxy, 100, 0, expectedStr);

        await _assertFinalBuffer(proxy, file, str, expectedStr);
      });

      test('write to end of initial len', () async {
        const str = 'test_str';
        final file = _createReadWriteFile(str, capacity: str.length + 2);
        final proxy = file.proxy;
        await _resetSeek(proxy, str.length);

        // write at end
        await _assertWrite(proxy, _newStrList, expectedSize: 2);

        final expectedStr = str + _newStr.substring(0, 2);
        await _assertReadAt(proxy, 100, 0, expectedStr);

        await _assertFinalBuffer(proxy, file, str, expectedStr);
      });

      test('writeAt should not depend on seek', () async {
        const str = 'test_str';
        final file = _createReadWriteFile(str, capacity: str.length + 2);
        final proxy = file.proxy;
        await _resetSeek(proxy, str.length);

        // seek is at end, write at 2nd position
        await _assertWriteAt(proxy, _newStrList, 2, _newStrList.length);

        // seek should not have changed
        final expectedStr = str.substring(0, 2) + _newStr;
        await _assertRead(proxy, 100, expectedStr.substring(str.length));

        await _assertFinalBuffer(proxy, file, str, expectedStr);
      });

      test('writeAt - end of file with', () async {
        const str = 'test_str';

        // writeAt should be able to write at end of the file
        final file = _createReadWriteFile(str, capacity: str.length + 5);
        final proxy = file.proxy;

        await _assertWriteAt(proxy, _newStrList, str.length, 5);

        final expectedStr = str + _newStr.substring(0, 5);
        await _assertRead(proxy, 100, expectedStr);

        await _assertFinalBuffer(proxy, file, str, expectedStr);
      });

      test('writeAt should fail for trying to write beyond end of file',
          () async {
        const str = 'test_str';
        final file = _createReadWriteFile(str, capacity: str.length + 5);
        final proxy = file.proxy;

        await _assertWriteAt(proxy, _newStrList, str.length + 1, 0,
            expectedStatus: ZX.ERR_OUT_OF_RANGE);

        await _assertRead(proxy, 100, str);

        await _assertFinalBuffer(proxy, file, str, str);
      });

      test('write functions fails for NodeReference flag', () async {
        const str = 'test_str';
        final file = _createReadWriteFile(str,
            capacity: str.length + 5,
            flags: OpenFlags.rightWritable | OpenFlags.nodeReference);
        final proxy = file.proxy;

        await _assertWriteAt(proxy, _newStrList, str.length + 1, 0,
            expectedStatus: ZX.ERR_ACCESS_DENIED);

        await _assertWrite(proxy, _newStrList,
            expectedSize: 0, expectedStatus: ZX.ERR_ACCESS_DENIED);
      });
    });

    test('various seek positions and reads', () async {
      const str = 'a very big string';
      final file = _createReadOnlyFile(str, OpenFlags.rightReadable);
      final proxy = file.proxy;

      {
        const c = 5;
        final data = await proxy.read(5);
        expect(String.fromCharCodes(data), str.substring(0, c));
      }

      var lastOffset = 5;
      {
        const c = 6;
        final data = await proxy.read(6);
        expect(String.fromCharCodes(data),
            str.substring(lastOffset, lastOffset + c));
      }

      {
        const c = 10;
        int offsetFromStart = await proxy.seek(SeekOrigin.start, c);
        expect(offsetFromStart, c);
        final data = await proxy.read(100);
        expect(String.fromCharCodes(data), str.substring(c));
      }

      {
        const c = 2;
        final offsetFromStart = await proxy.seek(SeekOrigin.start, c);
        expect(offsetFromStart, c);
        lastOffset = c;
      }

      {
        const c = 5;
        final offsetFromStart = await proxy.seek(SeekOrigin.current, c);
        expect(offsetFromStart, c + lastOffset);
        lastOffset = offsetFromStart;
        final data = await proxy.read(100);
        expect(String.fromCharCodes(data), str.substring(lastOffset));
      }

      {
        const c = 0;
        final offsetFromStart = await proxy.seek(SeekOrigin.end, c);
        expect(offsetFromStart, str.length - 1);
      }

      {
        const c = -3;
        final offsetFromStart = await proxy.seek(SeekOrigin.end, c);
        expect(offsetFromStart, str.length - 1 + c);
        final data = await proxy.read(100);
        expect(String.fromCharCodes(data), str.substring(offsetFromStart));
      }

      // Check edge and error conditions
      {
        const c = 3;
        await expectLater(
            proxy.seek(SeekOrigin.end, c),
            throwsA(isA<MethodException>()
                .having((e) => e.value, 'value', equals(ZX.ERR_OUT_OF_RANGE))));
      }

      {
        const c = -3;
        await expectLater(
            proxy.seek(SeekOrigin.start, c),
            throwsA(isA<MethodException>()
                .having((e) => e.value, 'value', equals(ZX.ERR_OUT_OF_RANGE))));
      }

      {
        const c = 5;
        final offsetFromStart = await proxy.seek(SeekOrigin.start, c);
        await expectLater(
            proxy.seek(SeekOrigin.current, str.length - offsetFromStart + 1),
            throwsA(isA<MethodException>()
                .having((e) => e.value, 'value', equals(ZX.ERR_OUT_OF_RANGE))));
      }
    });

    group('resize: ', () {
      test('failure test', () async {
        const str = 'test_str';
        final file = _createReadWriteFile(str, createProxy: false);

        final proxy = FileProxy();
        expect(
            file.pseudoFile.connect(
                OpenFlags.rightReadable, 0, _getNodeInterfaceRequest(proxy)),
            ZX.OK);
        await expectLater(
            proxy.resize(3),
            throwsA(isA<MethodException>().having(
                (e) => e.value, 'value', equals(ZX.ERR_ACCESS_DENIED))));

        await _assertRead(proxy, 100, str);

        await _assertFinalBuffer(proxy, file, str, str);
      });

      test('basic', () async {
        const str = 'test_str';
        final file = _createReadWriteFile(str);
        final proxy = file.proxy;

        await proxy.resize(3);

        final expectedStr = str.substring(0, 3);

        await _assertRead(proxy, 100, expectedStr);

        await _assertFinalBuffer(proxy, file, str, expectedStr);
      });

      test('on open', () async {
        const str = 'test_str';
        final file = _createReadWriteFile(str, createProxy: false);
        final proxy = FileProxy();
        expect(
            file.pseudoFile.connect(
                OpenFlags.rightReadable |
                    OpenFlags.rightWritable |
                    OpenFlags.truncate,
                0,
                _getNodeInterfaceRequest(proxy)),
            ZX.OK);

        await _assertFinalBuffer(proxy, file, str, '');
      });

      test('with seek', () async {
        const str = 'test_str';
        final file = _createReadWriteFile(str);
        final proxy = file.proxy;

        await _resetSeek(proxy, 5);

        await proxy.resize(3);

        // seek and currentLen should be 3 so we should not get any data
        await _assertRead(proxy, 100, '');

        // resize should have worked
        await _assertReadAt(proxy, 100, 0, str.substring(0, 3));
      });

      test('close should work', () async {
        const str = 'test_str';
        final file = _createReadOnlyFile(str, OpenFlags.rightReadable);
        // make sure file was opened
        file.pseudoFile.close();

        file.proxy.ctrl.whenClosed.asStream().listen(expectAsync1((_) {}));
      });
    });
  });
}

class _ReadOnlyFile {
  late PseudoFile pseudoFile;
  late FileProxy proxy;
}

class _ReadWriteFile {
  String? buffer;
  late PseudoFile pseudoFile;
  late FileProxy proxy;
}

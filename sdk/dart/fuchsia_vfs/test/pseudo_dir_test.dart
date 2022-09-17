// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';
import 'dart:convert';
import 'dart:typed_data';

import 'package:fidl/fidl.dart';
import 'package:fidl_fuchsia_io/fidl_async.dart';
import 'package:fuchsia_vfs/vfs.dart';
import 'package:test/test.dart';
import 'package:zircon/zircon.dart';

void main() {
  group('pseudo dir: ', () {
    test('inode number', () {
      Vnode dir = PseudoDir();
      expect(dir.inodeNumber(), inoUnknown);
    });

    test('type', () {
      Vnode dir = PseudoDir();
      expect(dir.type(), DirentType.directory);
    });

    test('basic', () {
      PseudoDir dir = PseudoDir();
      const key1 = 'key1';
      const key2 = 'key2';

      final node1 = _TestVnode();
      expect(dir.addNode(key1, node1), ZX.OK);
      expect(dir.lookup(key1), node1);

      final node2 = _TestVnode();
      expect(dir.addNode(key2, node2), ZX.OK);
      expect(dir.lookup(key2), node2);

      // make sure key1 is still there
      expect(dir.lookup(key1), node1);
    });

    test('legal name', () {
      PseudoDir dir = PseudoDir();
      const maxObjectNameLength = 255;
      StringBuffer specialsNonPrintablesBuilder = StringBuffer();
      // null character is illegal, start at one
      for (int char = 1; char < maxObjectNameLength; char++) {
        if (char != 47 /* key seperator */) {
          specialsNonPrintablesBuilder.writeCharCode(char);
        }
      }
      final legalKeys = <String>[
        'k',
        'key',
        'longer_key',
        // dart linter prefers interpolation over cat
        'just_shy_of_max_key${'_' * 236}',
        '.prefix_is_independently_illegal',
        '..prefix_is_independently_illegal',
        'suffix_is_independetly_illegal.',
        'suffix_is_independetly_illegal..',
        'infix_is_._independetly_illegal',
        'infix_is_.._independetly_illegal',
        '...',
        '....',
        'space is legal',
        'which\tmakes\tme\tuncomfortable',
        'very\nuncomfortable',
        'numbers_0123456789',
        specialsNonPrintablesBuilder.toString(),
      ];
      for (final key in legalKeys) {
        final node = _TestVnode();
        expect(dir.addNode(key, node), ZX.OK);
        expect(dir.lookup(key), node);
      }
    });

    test('illegal name', () {
      PseudoDir dir = PseudoDir();
      final illegalKeys = <String>[
        '',
        'illegal_length_key${'_' * 238}',
        '.',
        '..',
        '\u{00}',
        'null_\u{00}_character',
        '/',
        'key_/_seperator',
      ];
      final node = _TestVnode();
      for (final key in illegalKeys) {
        expect(dir.addNode(key, node), ZX.ERR_INVALID_ARGS);
      }
    });

    test('duplicate key', () {
      PseudoDir dir = PseudoDir();
      const key = 'key';
      final node = _TestVnode();
      final dupNode = _TestVnode();
      expect(dir.addNode(key, node), ZX.OK);
      expect(dir.addNode(key, dupNode), ZX.ERR_ALREADY_EXISTS);

      // check that key was not replaced
      expect(dir.lookup(key), node);
    });

    test('remove node', () {
      PseudoDir dir = PseudoDir();
      const key = 'key';
      final node = _TestVnode();
      expect(dir.addNode(key, node), ZX.OK);
      expect(dir.lookup(key), node);

      expect(dir.removeNode(key), ZX.OK);
      expect(dir.lookup(key), null);

      // add again and check
      expect(dir.addNode(key, node), ZX.OK);
      expect(dir.lookup(key), node);
    });

    test('remove when multiple keys', () {
      PseudoDir dir = PseudoDir();
      const key1 = 'key1';
      const key2 = 'key2';
      final node1 = _TestVnode();
      final node2 = _TestVnode();
      expect(dir.addNode(key1, node1), ZX.OK);
      expect(dir.addNode(key2, node2), ZX.OK);
      expect(dir.lookup(key1), node1);
      expect(dir.lookup(key2), node2);

      expect(dir.removeNode(key1), ZX.OK);
      expect(dir.lookup(key1), null);

      // check that key2 is still there
      expect(dir.lookup(key2), node2);

      // add again and check
      expect(dir.addNode(key1, node1), ZX.OK);
      expect(dir.lookup(key1), node1);
      expect(dir.lookup(key2), node2);
    });

    test('key order is maintained', () {
      PseudoDir dir = PseudoDir();
      const key1 = 'key1';
      const key2 = 'key2';
      const key3 = 'key3';
      final node1 = _TestVnode();
      final node2 = _TestVnode();
      final node3 = _TestVnode();
      expect(dir.addNode(key1, node1), ZX.OK);
      expect(dir.addNode(key2, node2), ZX.OK);
      expect(dir.addNode(key3, node3), ZX.OK);

      expect(dir.listNodeNames(), [key1, key2, key3]);

      // order maintained after removing node
      expect(dir.removeNode(key1), ZX.OK);
      expect(dir.listNodeNames(), [key2, key3]);

      // add again and check
      expect(dir.addNode(key1, node1), ZX.OK);
      expect(dir.listNodeNames(), [key2, key3, key1]);
    });

    test('remove and isEmpty', () {
      PseudoDir dir = PseudoDir();
      const key1 = 'key1';
      const key2 = 'key2';
      const key3 = 'key3';
      final node1 = _TestVnode();
      final node2 = _TestVnode();
      final node3 = _TestVnode();
      expect(dir.isEmpty(), true);
      expect(dir.addNode(key1, node1), ZX.OK);
      expect(dir.addNode(key2, node2), ZX.OK);
      expect(dir.addNode(key3, node3), ZX.OK);
      expect(dir.isEmpty(), false);

      expect(dir.removeNode(key1), ZX.OK);
      expect(dir.isEmpty(), false);
      dir.removeAllNodes();
      expect(dir.isEmpty(), true);
      expect(dir.listNodeNames(), []);
      // make sure that keys are really gone
      expect(dir.lookup(key2), null);
      expect(dir.lookup(key3), null);

      // add again and check
      expect(dir.addNode(key1, node1), ZX.OK);
      expect(dir.isEmpty(), false);
      expect(dir.lookup(key1), node1);
      expect(dir.listNodeNames(), [key1]);
    });
  });

  group('pseudo dir server: ', () {
    group('open fails: ', () {
      test('invalid flags', () async {
        PseudoDir dir = PseudoDir();
        final invalidFlags = [
          OpenFlags.append,
          OpenFlags.create,
          OpenFlags.createIfAbsent,
          OpenFlags.truncate,
        ];
        for (final entry in invalidFlags.asMap().entries) {
          DirectoryProxy proxy = DirectoryProxy();
          final status = dir.connect(entry.value | OpenFlags.describe, 0,
              InterfaceRequest(proxy.ctrl.request().passChannel()));
          expect(status, isNot(ZX.OK), reason: 'flagIndex: ${entry.key}');
          await proxy.onOpen.first.then((response) {
            expect(response.s, status);
            expect(response.info, isNull);
          }).catchError((err) async {
            fail(err.toString());
          });
        }
      });

      test('invalid mode', () async {
        PseudoDir dir = PseudoDir();
        final invalidModes = [
          modeTypeBlockDevice,
          modeTypeFile,
          modeTypeService,
          modeTypeService,
        ];

        for (final entry in invalidModes.asMap().entries) {
          DirectoryProxy proxy = DirectoryProxy();
          final status = dir.connect(OpenFlags.describe, entry.value,
              InterfaceRequest(proxy.ctrl.request().passChannel()));
          expect(status, ZX.ERR_INVALID_ARGS,
              reason: 'modeIndex: ${entry.key}');
          await proxy.onOpen.first.then((response) {
            expect(response.s, status);
            expect(response.info, isNull);
          }).catchError((err) async {
            fail(err.toString());
          });
        }
      });
    });

    DirectoryProxy _getProxyForDir(PseudoDir dir, [OpenFlags? flags]) {
      DirectoryProxy proxy = DirectoryProxy();
      final status = dir.connect(
          flags ?? OpenFlags.rightReadable | OpenFlags.rightWritable,
          0,
          InterfaceRequest(proxy.ctrl.request().passChannel()));
      expect(status, ZX.OK);
      return proxy;
    }

    test('open passes', () async {
      PseudoDir dir = PseudoDir();
      DirectoryProxy proxy =
          _getProxyForDir(dir, OpenFlags.rightReadable | OpenFlags.describe);

      await proxy.onOpen.first.then((response) {
        expect(response.s, ZX.OK);
        expect(response.info, isNotNull);
      }).catchError((err) async {
        fail(err.toString());
      });
    });

    test('open passes with valid mode', () async {
      PseudoDir dir = PseudoDir();
      final validModes = [
        modeProtectionMask,
        modeTypeDirectory,
      ];

      for (final entry in validModes.asMap().entries) {
        DirectoryProxy proxy = DirectoryProxy();
        final status = dir.connect(OpenFlags.describe, entry.value,
            InterfaceRequest(proxy.ctrl.request().passChannel()));
        expect(status, ZX.OK, reason: 'modeIndex: ${entry.key}');
        await proxy.onOpen.first.then((response) {
          expect(response.s, ZX.OK);
          expect(response.info, isNotNull);
        }).catchError((err) async {
          fail(err.toString());
        });
      }
    });

    test('open passes with valid flags', () async {
      PseudoDir dir = PseudoDir();
      final validFlags = [
        OpenFlags.rightReadable,
        OpenFlags.rightWritable,
        OpenFlags.rightReadable | OpenFlags.directory,
        OpenFlags.nodeReference
      ];

      for (final flag in validFlags) {
        DirectoryProxy proxy = _getProxyForDir(dir, flag | OpenFlags.describe);
        await proxy.onOpen.first.then((response) {
          expect(response.s, ZX.OK);
          expect(response.info, isNotNull);
        }).catchError((err) async {
          fail(err.toString());
        });
      }
    });

    test('getattr', () async {
      PseudoDir dir = PseudoDir();
      DirectoryProxy proxy = _getProxyForDir(dir);

      final attr = await proxy.getAttr();

      expect(attr.attributes.linkCount, 1);
      expect(attr.attributes.mode, modeProtectionMask | modeTypeDirectory);
    });

    _Dirent _createDirentForDot() {
      return _Dirent(inoUnknown, 1, DirentType.directory, '.');
    }

    _Dirent _createDirent(Vnode vnode, String name) {
      return _Dirent(vnode.inodeNumber(), name.length, vnode.type(), name);
    }

    int _expectedDirentSize(List<_Dirent> dirents) {
      var sum = 0;
      for (final d in dirents) {
        sum += d.direntSizeInBytes!;
      }
      return sum;
    }

    void _validateExpectedDirents(
        List<_Dirent> dirents, Directory$ReadDirents$Response response) {
      expect(response.s, ZX.OK);
      expect(response.dirents.length, _expectedDirentSize(dirents));
      var offset = 0;
      for (final dirent in dirents) {
        final data = ByteData.view(
            response.dirents.buffer, response.dirents.offsetInBytes + offset);
        final actualDirent = _Dirent.fromData(data);
        expect(actualDirent, dirent);
        offset += actualDirent.direntSizeInBytes!;
      }
    }

    group('read dir:', () {
      test('simple call should work', () async {
        PseudoDir dir = PseudoDir();
        PseudoDir subDir = PseudoDir();
        final file1 = PseudoFile.readOnlyStr(() => 'file1');
        final file2 = PseudoFile.readOnlyStr(() => 'file2');
        final file3 = PseudoFile.readOnlyStr(() => 'file3');
        dir
          ..addNode('file1', file1)
          ..addNode('subDir', subDir)
          ..addNode('file3', file3);
        subDir.addNode('file2', file2);

        {
          final proxy = _getProxyForDir(dir);

          final expectedDirents = [
            _createDirentForDot(),
            _createDirent(file1, 'file1'),
            _createDirent(subDir, 'subDir'),
            _createDirent(file3, 'file3'),
          ];
          {
            final response = await proxy.readDirents(1024);
            _validateExpectedDirents(expectedDirents, response);
          }

          // test that next read call returns length zero buffer
          {
            final response = await proxy.readDirents(1024);
            expect(response.s, ZX.OK);
            expect(response.dirents.length, 0);
          }
        }

        // also test sub folder and make sure it was not affected by parent dir.
        {
          final proxy = _getProxyForDir(subDir);
          final expectedDirents = [
            _createDirentForDot(),
            _createDirent(file2, 'file2'),
          ];
          final response = await proxy.readDirents(1024);
          _validateExpectedDirents(expectedDirents, response);
        }
      });

      test('serve function works', () async {
        PseudoDir dir = PseudoDir();
        final file1 = PseudoFile.readOnlyStr(() => 'file1');

        dir.addNode('file1', file1);

        DirectoryProxy proxy = DirectoryProxy();
        final status =
            dir.serve(InterfaceRequest(proxy.ctrl.request().passChannel()));
        expect(status, ZX.OK);

        final expectedDirents = [
          _createDirentForDot(),
          _createDirent(file1, 'file1'),
        ];
        final response = await proxy.readDirents(1024);
        _validateExpectedDirents(expectedDirents, response);
      });

      test('passed buffer size is exact', () async {
        PseudoDir dir = PseudoDir();
        PseudoDir subDir = PseudoDir();
        final file1 = PseudoFile.readOnlyStr(() => 'file1');
        final file3 = PseudoFile.readOnlyStr(() => 'file3');
        dir
          ..addNode('file1', file1)
          ..addNode('subDir', subDir)
          ..addNode('file3', file3);
        final proxy = _getProxyForDir(dir);

        final expectedDirents = [
          _createDirentForDot(),
          _createDirent(file1, 'file1'),
          _createDirent(subDir, 'subDir'),
          _createDirent(file3, 'file3'),
        ];
        {
          final response =
              await proxy.readDirents(_expectedDirentSize(expectedDirents));
          _validateExpectedDirents(expectedDirents, response);
        }

        // test that next read call returns length zero buffer
        {
          final response = await proxy.readDirents(1024);
          expect(response.s, ZX.OK);
          expect(response.dirents.length, 0);
        }
      });

      test('passed buffer size is exact - 1', () async {
        PseudoDir dir = PseudoDir();
        PseudoDir subDir = PseudoDir();
        final file1 = PseudoFile.readOnlyStr(() => 'file1');
        final file3 = PseudoFile.readOnlyStr(() => 'file3');
        dir
          ..addNode('file1', file1)
          ..addNode('subDir', subDir)
          ..addNode('file3', file3);

        final proxy = _getProxyForDir(dir);

        final expectedDirents = [
          _createDirentForDot(),
          _createDirent(file1, 'file1'),
          _createDirent(subDir, 'subDir'),
          _createDirent(file3, 'file3'),
        ];
        final size = _expectedDirentSize(expectedDirents) - 1;
        final lastDirent = expectedDirents.removeLast();

        {
          final response = await proxy.readDirents(size);
          _validateExpectedDirents(expectedDirents, response);
        }

        // test that next read call returns last dirent
        {
          final response = await proxy.readDirents(1024);
          _validateExpectedDirents([lastDirent], response);
        }

        // test that next read call returns length zero buffer
        {
          final response = await proxy.readDirents(1024);
          expect(response.s, ZX.OK);
          expect(response.dirents.length, 0);
        }
      });

      test('buffer too small', () async {
        PseudoDir dir = PseudoDir();
        PseudoDir subDir = PseudoDir();
        final file1 = PseudoFile.readOnlyStr(() => 'file1');
        dir
          ..addNode('file1', file1)
          ..addNode('subDir', subDir);

        final proxy = _getProxyForDir(dir);

        final size = _expectedDirentSize([_createDirentForDot()]) - 1;
        for (int i = 0; i < size; i++) {
          final response = await proxy.readDirents(i);
          expect(response.s, ZX.ERR_BUFFER_TOO_SMALL);
          expect(response.dirents.length, 0);
        }
      });

      test(
          'buffer too small after first dot read and subsequent reads with bigger buffer returns correct dirents',
          () async {
        PseudoDir dir = PseudoDir();
        PseudoDir subDir = PseudoDir();
        final file1 = PseudoFile.readOnlyStr(() => 'file1');
        dir
          ..addNode('file1', file1)
          ..addNode('subDir', subDir);

        final proxy = _getProxyForDir(dir);
        final size = _expectedDirentSize([_createDirentForDot()]);

        {
          final response = await proxy.readDirents(size);
          // make sure that '.' was read
          _validateExpectedDirents([_createDirentForDot()], response);
        }

        // this should return error
        {
          final response = await proxy.readDirents(size);
          expect(response.s, ZX.ERR_BUFFER_TOO_SMALL);
          expect(response.dirents.length, 0);
        }

        final expectedDirents = [
          _createDirent(file1, 'file1'),
          _createDirent(subDir, 'subDir'),
        ];
        final response =
            await proxy.readDirents(_expectedDirentSize(expectedDirents));
        _validateExpectedDirents(expectedDirents, response);
      });

      test('multiple reads with small buffer', () async {
        PseudoDir dir = PseudoDir();
        PseudoDir subDir = PseudoDir();
        final file1 = PseudoFile.readOnlyStr(() => 'file1');
        dir
          ..addNode('file1', file1)
          ..addNode('subDir', subDir);

        final proxy = _getProxyForDir(dir);
        final expectedDirents = [
          _createDirentForDot(),
          _createDirent(file1, 'file1'),
          _createDirent(subDir, 'subDir'),
        ];
        for (final dirent in expectedDirents) {
          final dirents = [dirent];
          final response =
              await proxy.readDirents(_expectedDirentSize(dirents));
          _validateExpectedDirents(dirents, response);
        }

        // test that next read call returns length zero buffer
        final response = await proxy.readDirents(1024);
        expect(response.s, ZX.OK);
        expect(response.dirents.length, 0);
      });

      test('read two dirents then one', () async {
        PseudoDir dir = PseudoDir();
        PseudoDir subDir = PseudoDir();
        final file1 = PseudoFile.readOnlyStr(() => 'file1');
        dir
          ..addNode('file1', file1)
          ..addNode('subDir', subDir);

        final proxy = _getProxyForDir(dir);

        {
          final expectedDirents = [
            _createDirentForDot(),
            _createDirent(file1, 'file1'),
          ];

          final response =
              await proxy.readDirents(_expectedDirentSize(expectedDirents));
          _validateExpectedDirents(expectedDirents, response);
        }

        {
          final expectedDirents = [
            _createDirent(subDir, 'subDir'),
          ];

          final response = await proxy.readDirents(1024);
          _validateExpectedDirents(expectedDirents, response);
        }
      });

      test('buffer size more than first less than 2 dirents', () async {
        PseudoDir dir = PseudoDir();
        PseudoDir subDir = PseudoDir();
        final file1 = PseudoFile.readOnlyStr(() => 'file1');
        dir
          ..addNode('file1', file1)
          ..addNode('subDir', subDir);

        final proxy = _getProxyForDir(dir);

        {
          final expectedDirents = [
            _createDirentForDot(),
          ];

          final response = await proxy
              .readDirents(_expectedDirentSize(expectedDirents) + 10);
          _validateExpectedDirents(expectedDirents, response);
        }

        {
          // now test that we are able to get rest
          final expectedDirents = [
            _createDirent(file1, 'file1'),
            _createDirent(subDir, 'subDir'),
          ];

          final response = await proxy.readDirents(1024);
          _validateExpectedDirents(expectedDirents, response);
        }
      });

      test('rewind works', () async {
        PseudoDir dir = PseudoDir();
        PseudoDir subDir = PseudoDir();
        final file1 = PseudoFile.readOnlyStr(() => 'file1');
        dir
          ..addNode('file1', file1)
          ..addNode('subDir', subDir);

        final proxy = _getProxyForDir(dir);

        final expectedDirents = [
          _createDirentForDot(),
        ];

        {
          final response = await proxy
              .readDirents(_expectedDirentSize(expectedDirents) + 10);
          _validateExpectedDirents(expectedDirents, response);
        }

        {
          final rewindResponse = await proxy.rewind();
          expect(rewindResponse, ZX.OK);
        }

        {
          final response = await proxy
              .readDirents(_expectedDirentSize(expectedDirents) + 10);
          _validateExpectedDirents(expectedDirents, response);
        }
      });

      test('rewind works after we reach directory end', () async {
        PseudoDir dir = PseudoDir();
        PseudoDir subDir = PseudoDir();
        final file1 = PseudoFile.readOnlyStr(() => 'file1');
        dir
          ..addNode('file1', file1)
          ..addNode('subDir', subDir);

        final proxy = _getProxyForDir(dir);

        final expectedDirents = [
          _createDirentForDot(),
          _createDirent(file1, 'file1'),
          _createDirent(subDir, 'subDir'),
        ];

        {
          final response = await proxy
              .readDirents(_expectedDirentSize(expectedDirents) + 10);
          _validateExpectedDirents(expectedDirents, response);
        }

        {
          final rewindResponse = await proxy.rewind();
          expect(rewindResponse, ZX.OK);
        }

        {
          final response = await proxy
              .readDirents(_expectedDirentSize(expectedDirents) + 10);
          _validateExpectedDirents(expectedDirents, response);
        }
      });

      test('readdir works when node removed', () async {
        PseudoDir dir = PseudoDir();
        PseudoDir subDir = PseudoDir();
        final file1 = PseudoFile.readOnlyStr(() => 'file1');
        dir
          ..addNode('file1', file1)
          ..addNode('subDir', subDir);

        final proxy = _getProxyForDir(dir);

        {
          final expectedDirents = [
            _createDirentForDot(),
          ];
          final response =
              await proxy.readDirents(_expectedDirentSize(expectedDirents));
          _validateExpectedDirents(expectedDirents, response);
        }

        // remove first node
        dir.removeNode('file1');

        {
          final expectedDirents = [_createDirent(subDir, 'subDir')];
          final response = await proxy.readDirents(1024);
          _validateExpectedDirents(expectedDirents, response);
        }
      });

      test('readdir works when already last node is removed', () async {
        PseudoDir dir = PseudoDir();
        PseudoDir subDir = PseudoDir();
        final file1 = PseudoFile.readOnlyStr(() => 'file1');
        dir
          ..addNode('file1', file1)
          ..addNode('subDir', subDir);

        final proxy = _getProxyForDir(dir);

        {
          final expectedDirents = [
            _createDirentForDot(),
            _createDirent(file1, 'file1')
          ];
          final response =
              await proxy.readDirents(_expectedDirentSize(expectedDirents));
          _validateExpectedDirents(expectedDirents, response);
        }

        // remove first node
        dir.removeNode('file1');

        {
          final expectedDirents = [_createDirent(subDir, 'subDir')];
          final response = await proxy.readDirents(1024);
          _validateExpectedDirents(expectedDirents, response);
        }
      });

      test('readdir works when node is added', () async {
        PseudoDir dir = PseudoDir();
        PseudoDir subDir = PseudoDir();
        final file1 = PseudoFile.readOnlyStr(() => 'file1');
        dir
          ..addNode('file1', file1)
          ..addNode('subDir', subDir);

        final proxy = _getProxyForDir(dir);

        {
          final expectedDirents = [
            _createDirentForDot(),
            _createDirent(file1, 'file1'),
            _createDirent(subDir, 'subDir')
          ];
          final response =
              await proxy.readDirents(_expectedDirentSize(expectedDirents));
          _validateExpectedDirents(expectedDirents, response);
        }

        dir.addNode('file2', file1);

        {
          final expectedDirents = [_createDirent(file1, 'file2')];
          final response = await proxy.readDirents(1024);
          _validateExpectedDirents(expectedDirents, response);
        }
      });

      test('readdir works when node is added and only first node was read',
          () async {
        PseudoDir dir = PseudoDir();
        PseudoDir subDir = PseudoDir();
        final file1 = PseudoFile.readOnlyStr(() => 'file1');
        dir
          ..addNode('file1', file1)
          ..addNode('subDir', subDir);

        final proxy = _getProxyForDir(dir);

        {
          final expectedDirents = [
            _createDirentForDot(),
          ];
          final response =
              await proxy.readDirents(_expectedDirentSize(expectedDirents));
          _validateExpectedDirents(expectedDirents, response);
        }

        dir.addNode('file2', file1);

        {
          final expectedDirents = [
            _createDirent(file1, 'file1'),
            _createDirent(subDir, 'subDir'),
            _createDirent(file1, 'file2')
          ];
          final response = await proxy.readDirents(1024);
          _validateExpectedDirents(expectedDirents, response);
        }
      });
    });

    group('open/close file/dir in dir:', () {
      Future<void> _openFileAndAssert(DirectoryProxy proxy, String filePath,
          int bufferLen, String expectedContent) async {
        FileProxy fileProxy = FileProxy();
        await proxy.open(OpenFlags.rightReadable, 0, filePath,
            InterfaceRequest(fileProxy.ctrl.request().passChannel()));

        final data = await fileProxy.read(bufferLen);
        expect(String.fromCharCodes(data), expectedContent);
      }

      PseudoDir _setUpDir() {
        PseudoDir dir = PseudoDir();
        PseudoDir subDir = PseudoDir();
        final file1 = PseudoFile.readOnlyStr(() => 'file1');
        final file2 = PseudoFile.readOnlyStr(() => 'file2');
        final file3 = PseudoFile.readOnlyStr(() => 'file3');
        final file4 = PseudoFile.readOnlyStr(() => 'file4');
        dir
          ..addNode('file1', file1)
          ..addNode('subDir', subDir)
          ..addNode('file3', file3);
        subDir
          ..addNode('file2', file2)
          ..addNode('file4', file4);
        return dir;
      }

      test('open self', () async {
        PseudoDir dir = _setUpDir();

        final proxy = _getProxyForDir(dir);
        final paths = ['.'];
        for (final path in paths) {
          DirectoryProxy newProxy = DirectoryProxy();
          await proxy.open(OpenFlags.rightReadable, 0, path,
              InterfaceRequest(newProxy.ctrl.request().passChannel()));

          // open file 1 in proxy and check contents to make sure correct dir was opened.
          await _openFileAndAssert(newProxy, 'file1', 100, 'file1');
        }
      });

      test('open file', () async {
        PseudoDir dir = _setUpDir();

        final proxy = _getProxyForDir(dir);

        // open file 1 check contents.
        final paths = ['file1', '/file1'];
        for (final path in paths) {
          await _openFileAndAssert(proxy, path, 100, 'file1');
        }
      });

      test('open fails for illegal path', () async {
        PseudoDir dir = _setUpDir();

        final proxy = _getProxyForDir(dir);
        final paths = <String>[
          '',
          'too_long_path${'_' * 242}',
          'subDir/too_long_path${'_' * 242}',
          '..',
          'subDir/..',
          'invalid_\u{00}_name',
          'subDir/invalid_\u{00}_name',
          'invalid_\u{00}_name/legal_name',
        ];
        for (final path in paths) {
          DirectoryProxy newProxy = DirectoryProxy();
          await proxy.open(OpenFlags.rightReadable | OpenFlags.describe, 0,
              path, InterfaceRequest(newProxy.ctrl.request().passChannel()));

          await newProxy.onOpen.first.then((response) {
            expect(response.s, isNot(ZX.OK));
            expect(response.info, isNull);
          }).catchError((err) async {
            fail(err.toString());
          });
        }
      });

      test('open file fails for path ending with "/"', () async {
        PseudoDir dir = _setUpDir();

        final proxy = _getProxyForDir(dir);

        FileProxy fileProxy = FileProxy();
        await proxy.open(OpenFlags.rightReadable | OpenFlags.describe, 0,
            'file1/', InterfaceRequest(fileProxy.ctrl.request().passChannel()));

        await fileProxy.onOpen.first.then((response) {
          expect(response.s, ZX.ERR_NOT_DIR);
          expect(response.info, isNull);
        }).catchError((err) async {
          fail(err.toString());
        });
      });

      test('open file fails for invalid key', () async {
        PseudoDir dir = _setUpDir();

        final proxy = _getProxyForDir(dir);

        FileProxy fileProxy = FileProxy();
        await proxy.open(OpenFlags.rightReadable, 0, 'invalid',
            InterfaceRequest(fileProxy.ctrl.request().passChannel()));

        // channel should be closed
        fileProxy.ctrl.whenClosed.asStream().listen(expectAsync1((_) {}));
      });

      test('open fails for trying to open file within a file', () async {
        PseudoDir dir = _setUpDir();

        final proxy = _getProxyForDir(dir);

        FileProxy fileProxy = FileProxy();
        await proxy.open(
            OpenFlags.rightReadable | OpenFlags.describe,
            0,
            'file1/file2',
            InterfaceRequest(fileProxy.ctrl.request().passChannel()));

        await fileProxy.onOpen.first.then((response) {
          expect(response.s, ZX.ERR_NOT_DIR);
          expect(response.info, isNull);
        }).catchError((err) async {
          fail(err.toString());
        });
      });

      test('close works', () async {
        PseudoDir dir = PseudoDir();
        PseudoDir subDir = PseudoDir();
        final file1 = PseudoFile.readOnlyStr(() => 'file1');
        dir
          ..addNode('file1', file1)
          ..addNode('subDir', subDir);

        final proxy = _getProxyForDir(dir);
        DirectoryProxy subDirProxy = DirectoryProxy();
        await proxy.open(OpenFlags.rightReadable, 0, 'subDir',
            InterfaceRequest(subDirProxy.ctrl.request().passChannel()));

        FileProxy fileProxy = FileProxy();
        await proxy.open(OpenFlags.rightReadable, 0, 'file1',
            InterfaceRequest(fileProxy.ctrl.request().passChannel()));
        dir.close();
        proxy.ctrl.whenClosed.asStream().listen(expectAsync1((_) {}));
        subDirProxy.ctrl.whenClosed.asStream().listen(expectAsync1((_) {}));
        fileProxy.ctrl.whenClosed.asStream().listen(expectAsync1((_) {}));
      });

      test('open sub dir', () async {
        PseudoDir dir = _setUpDir();

        final proxy = _getProxyForDir(dir);

        DirectoryProxy dirProxy = DirectoryProxy();
        await proxy.open(OpenFlags.rightReadable, 0, 'subDir',
            InterfaceRequest(dirProxy.ctrl.request().passChannel()));

        // open file 2 check contents to make sure correct dir was opened.
        await _openFileAndAssert(dirProxy, 'file2', 100, 'file2');
      });

      test('directory rights are hierarchical (open dir)', () async {
        PseudoDir dir = _setUpDir();

        final proxy = _getProxyForDir(dir, OpenFlags.rightReadable);

        final newProxy = DirectoryProxy();
        await proxy.open(OpenFlags.rightWritable | OpenFlags.describe, 0,
            'subDir', InterfaceRequest(newProxy.ctrl.request().passChannel()));

        await newProxy.onOpen.first.then((response) {
          expect(response.s, ZX.ERR_ACCESS_DENIED);
          expect(response.info, isNull);
        }).catchError((err) async {
          fail(err.toString());
        });
      });

      test('directory rights are hierarchical (open file)', () async {
        PseudoDir dir = _setUpDir();

        final proxy = _getProxyForDir(dir, OpenFlags.rightWritable);

        final newProxy = DirectoryProxy();
        await proxy.open(OpenFlags.rightWritable | OpenFlags.describe, 0,
            'subDir', InterfaceRequest(newProxy.ctrl.request().passChannel()));

        await newProxy.onOpen.first.then((response) {
          expect(response.s, ZX.OK);
          expect(response.info, isNotNull);
        }).catchError((err) async {
          fail(err.toString());
        });

        FileProxy fileProxy = FileProxy();
        await newProxy.open(OpenFlags.rightReadable, 0, 'file2',
            InterfaceRequest(fileProxy.ctrl.request().passChannel()));

        // channel should be closed
        fileProxy.ctrl.whenClosed.asStream().listen(expectAsync1((_) {}));
      });

      test('open sub dir with "/" at end', () async {
        PseudoDir dir = _setUpDir();

        final proxy = _getProxyForDir(dir);

        DirectoryProxy dirProxy = DirectoryProxy();
        await proxy.open(OpenFlags.rightReadable, 0, 'subDir/',
            InterfaceRequest(dirProxy.ctrl.request().passChannel()));

        // open file 2 check contents to make sure correct dir was opened.
        await _openFileAndAssert(dirProxy, 'file2', 100, 'file2');
      });

      test('directly open file in sub dir', () async {
        PseudoDir dir = _setUpDir();

        final proxy = _getProxyForDir(dir);

        // open file 2 in subDir.
        await _openFileAndAssert(proxy, 'subDir/file2', 100, 'file2');
      });

      test('readdir fails for NodeReference', () async {
        PseudoDir dir = _setUpDir();

        final proxy = _getProxyForDir(dir, OpenFlags.nodeReference);

        final response = await proxy.readDirents(1024);
        expect(response.s, ZX.ERR_BAD_HANDLE);
      });

      test('not allowed to open a file for NodeReference', () async {
        PseudoDir dir = _setUpDir();

        final proxy = _getProxyForDir(dir, OpenFlags.nodeReference);

        // open file 2 in subDir.
        FileProxy fileProxy = FileProxy();
        await proxy.open(OpenFlags.rightReadable, 0, 'subDir/file2',
            InterfaceRequest(fileProxy.ctrl.request().passChannel()));

        // channel should be closed
        fileProxy.ctrl.whenClosed.asStream().listen(expectAsync1((_) {}));
      });

      test('clone with same rights', () async {
        PseudoDir dir = _setUpDir();

        final proxy = _getProxyForDir(
            dir, OpenFlags.rightReadable | OpenFlags.rightWritable);
        DirectoryProxy cloneProxy = DirectoryProxy();
        await proxy.clone(OpenFlags.cloneSameRights | OpenFlags.describe,
            InterfaceRequest(cloneProxy.ctrl.request().passChannel()));

        final subDirProxy = DirectoryProxy();
        await cloneProxy.open(
            OpenFlags.rightReadable |
                OpenFlags.rightWritable |
                OpenFlags.describe,
            0,
            'subDir',
            InterfaceRequest(subDirProxy.ctrl.request().passChannel()));

        await subDirProxy.onOpen.first.then((response) {
          expect(response.s, ZX.OK);
          expect(response.info, isNotNull);
        }).catchError((err) async {
          fail(err.toString());
        });

        // open file 2 check contents to make sure correct dir was opened.
        await _openFileAndAssert(subDirProxy, 'file2', 100, 'file2');
      });
    });

    test('test clone', () async {
      PseudoDir dir = PseudoDir();

      final proxy = _getProxyForDir(dir, OpenFlags.rightReadable);

      DirectoryProxy newProxy = DirectoryProxy();
      await proxy.clone(OpenFlags.rightReadable | OpenFlags.describe,
          InterfaceRequest(newProxy.ctrl.request().passChannel()));

      await newProxy.onOpen.first.then((response) {
        expect(response.s, ZX.OK);
        expect(response.info, isNotNull);
      }).catchError((err) async {
        fail(err.toString());
      });
    });

    test('clone should fail if requested rights exceed source rights',
        () async {
      PseudoDir dir = PseudoDir();
      final proxy = _getProxyForDir(dir, OpenFlags.rightReadable);

      final clonedProxy = DirectoryProxy();
      await proxy.clone(OpenFlags.rightWritable | OpenFlags.describe,
          InterfaceRequest(clonedProxy.ctrl.request().passChannel()));

      await clonedProxy.onOpen.first.then((response) {
        expect(response.s, ZX.ERR_ACCESS_DENIED);
        expect(response.info, isNull);
      }).catchError((err) async {
        fail(err.toString());
      });
    });

    test('test clone fails for invalid flags', () async {
      PseudoDir dir = PseudoDir();

      final proxy = _getProxyForDir(dir, OpenFlags.rightReadable);

      DirectoryProxy newProxy = DirectoryProxy();
      await proxy.clone(OpenFlags.truncate | OpenFlags.describe,
          InterfaceRequest(newProxy.ctrl.request().passChannel()));

      await newProxy.onOpen.first.then((response) {
        expect(response.s, isNot(ZX.OK));
        expect(response.info, isNull);
      }).catchError((err) async {
        fail(err.toString());
      });
    });

    test(
        'test clone disallows both OpenFlags.cloneSameRights and specific rights',
        () async {
      PseudoDir dir = PseudoDir();
      final proxy = _getProxyForDir(dir, OpenFlags.rightReadable);

      final clonedProxy = DirectoryProxy();
      await proxy.clone(
          OpenFlags.rightReadable |
              OpenFlags.cloneSameRights |
              OpenFlags.describe,
          InterfaceRequest(clonedProxy.ctrl.request().passChannel()));

      await clonedProxy.onOpen.first.then((response) {
        expect(response.s, ZX.ERR_INVALID_ARGS);
        expect(response.info, isNull);
      }).catchError((err) async {
        fail(err.toString());
      });
    });
  });
}

class _Dirent {
  static const int _fixedSize = 10;
  int? ino;
  int? size;
  DirentType? type;
  String? str;

  int? direntSizeInBytes;
  _Dirent(this.ino, this.size, this.type, this.str) {
    direntSizeInBytes = _fixedSize + size!;
  }

  _Dirent.fromData(ByteData data) {
    ino = data.getUint64(0, Endian.little);
    size = data.getUint8(8);
    type = DirentType(data.getUint8(9));
    var offset = _fixedSize;
    List<int> charBytes = [];
    direntSizeInBytes = offset + size!;
    expect(data.lengthInBytes, greaterThanOrEqualTo(direntSizeInBytes!));
    for (int i = 0; i < size!; i++) {
      charBytes.add(data.getUint8(offset++));
    }
    str = utf8.decode(charBytes);
  }

  @override
  int get hashCode =>
      ino.hashCode + size.hashCode + type.hashCode + str.hashCode;

  @override
  bool operator ==(Object o) {
    return o is _Dirent &&
        o.ino == ino &&
        o.size == size &&
        o.type == type &&
        o.str == str;
  }

  @override
  String toString() {
    return '[ino: $ino, size: $size, type: $type, str: $str]';
  }
}

class _TestVnode extends Vnode {
  final String _val = '';
  _TestVnode();

  @override
  int connect(OpenFlags flags, int mode, InterfaceRequest<Node> request,
      [OpenFlags? parentFlags]) {
    throw UnimplementedError();
  }

  @override
  int inodeNumber() {
    return inoUnknown;
  }

  @override
  DirentType type() {
    return DirentType.unknown;
  }

  String value() => _val;

  @override
  void close() {}
}

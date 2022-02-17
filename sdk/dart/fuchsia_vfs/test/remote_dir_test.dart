// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:convert' show utf8;

import 'package:fidl/fidl.dart';
import 'package:fidl_fuchsia_io/fidl_async.dart';
import 'package:fuchsia_vfs/vfs.dart';
import 'package:test/test.dart';
import 'package:zircon/zircon.dart';

void main() {
  group('remote dir: ', () {
    late PseudoDir _externalDirectory;
    late PseudoDir _localDirectory;
    late Channel _remoteDirHandle;

    setUp(() {
      final channelPair = ChannelPair();
      _externalDirectory = PseudoDir()
        ..addNode('file1.txt', PseudoFile.readOnlyStr(() => 'foo'))
        ..addNode(
            'bar',
            PseudoDir()
              ..addNode('file2.txt', PseudoFile.readOnlyStr(() => 'bar')))
        ..serve(InterfaceRequest<Node>(channelPair.first!));

      _remoteDirHandle = channelPair.second!;

      _localDirectory = PseudoDir();
    });

    tearDown(() {
      _externalDirectory.close();
      _localDirectory.close();
    });

    test('inode number', () {
      final dir = RemoteDir(_remoteDirHandle);
      expect(dir.inodeNumber(), inoUnknown);
    });

    test('type', () {
      final dir = RemoteDir(_remoteDirHandle);
      expect(dir.type(), direntTypeDirectory);
    });

    test('add remote after serve', () async {
      final proxy = _serveLocal(_localDirectory);
      _localDirectory.addNode('subdir', RemoteDir(_remoteDirHandle));

      final contents =
          await _readFileContentsInRemoteDir(proxy, 'subdir', 'file1.txt');
      expect(contents, 'foo');

      proxy.ctrl.close();
    });

    test('add remote before serve', () async {
      _localDirectory.addNode('subdir', RemoteDir(_remoteDirHandle));
      final proxy = _serveLocal(_localDirectory);

      final contents =
          await _readFileContentsInRemoteDir(proxy, 'subdir', 'file1.txt');
      expect(contents, 'foo');

      proxy.ctrl.close();
    });

    test('can open with trailing slash', () async {
      _localDirectory.addNode('subdir', RemoteDir(_remoteDirHandle));
      final proxy = _serveLocal(_localDirectory);

      final contents =
          await _readFileContentsInRemoteDir(proxy, 'subdir/', 'file1.txt');
      expect(contents, 'foo');

      proxy.ctrl.close();
    });

    test('can open subdir of remote', () async {
      _localDirectory.addNode('subdir', RemoteDir(_remoteDirHandle));
      final proxy = _serveLocal(_localDirectory);

      final contents =
          await _readFileContentsInRemoteDir(proxy, 'subdir', 'bar/file2.txt');
      expect(contents, 'bar');

      proxy.ctrl.close();
    });

    test('can open file when remote is not at root', () async {
      _localDirectory.addNode(
          'dir1', PseudoDir()..addNode('dir2', RemoteDir(_remoteDirHandle)));

      final proxy = _serveLocal(_localDirectory);

      final contents =
          await _readFileContentsInRemoteDir(proxy, 'dir1/dir2', 'file1.txt');
      expect(contents, 'foo');

      proxy.ctrl.close();
    });

    test('can open nested file when remote is not at root', () async {
      _localDirectory.addNode(
          'dir1', PseudoDir()..addNode('dir2', RemoteDir(_remoteDirHandle)));

      final proxy = _serveLocal(_localDirectory);

      final contents = await _readFileContentsInRemoteDir(
          proxy, 'dir1/dir2', 'bar/file2.txt');
      expect(contents, 'bar');

      proxy.ctrl.close();
    });

    test('Can open paths through the remote dir', () async {
      _localDirectory.addNode('subdir', RemoteDir(_remoteDirHandle));

      final proxy = _serveLocal(_localDirectory);

      expect(await _readFileContents(proxy, 'subdir/bar/file2.txt'), 'bar');
      expect(await _readFileContents(proxy, 'subdir/file1.txt'), 'foo');
      expect(await _readFileContents(proxy, 'subdir/./file1.txt'), 'foo');

      proxy.ctrl.close();
    });

    test('open with invalid flags', () async {
      _localDirectory.addNode('subdir', RemoteDir(_remoteDirHandle));
      final dir = _serveLocal(_localDirectory);

      // flags and statuses need to be kept in sync
      final flags = [openFlagNoRemote];
      final statuses = [ZX.ERR_NOT_SUPPORTED];
      expect(flags.length, statuses.length);

      for (int i = 0; i < flags.length; i++) {
        DirectoryProxy proxy = DirectoryProxy();
        await dir.open(openFlagDescribe | flags[i], 0, 'subdir',
            InterfaceRequest(proxy.ctrl.request().passChannel()));

        await proxy.onOpen.first.then((response) {
          expect(response.s, statuses[i]);
        }).catchError((err) async {
          fail(err.toString());
        });

        proxy.ctrl.close();
      }

      dir.ctrl.close();
    });

    test('connect with invalid flags', () async {
      final dir = RemoteDir(_remoteDirHandle);

      // flags and statuses need to be kept in sync
      final flags = [openFlagNoRemote];
      final statuses = [ZX.ERR_NOT_SUPPORTED];
      expect(flags.length, statuses.length);

      for (int i = 0; i < flags.length; i++) {
        DirectoryProxy proxy = DirectoryProxy();
        dir.connect(openFlagDescribe | flags[i], 0,
            InterfaceRequest(proxy.ctrl.request().passChannel()));

        await proxy.onOpen.first.then((response) {
          expect(response.s, statuses[i]);
        }).catchError((err) async {
          fail(err.toString());
        });
        proxy.ctrl.close();
      }

      dir.close();
    });

    test('open after close fails', () async {
      _localDirectory.addNode('subdir', RemoteDir(_remoteDirHandle)..close());
      final dir = _serveLocal(_localDirectory);

      DirectoryProxy proxy = DirectoryProxy();
      await dir.open(openFlagDescribe, 0, 'subdir',
          InterfaceRequest(proxy.ctrl.request().passChannel()));

      await proxy.onOpen.first.then((response) {
        expect(response.s, ZX.ERR_NOT_SUPPORTED);
      }).catchError((err) async {
        fail(err.toString());
      });

      proxy.ctrl.close();
      dir.ctrl.close();
    });

    test('connect after close failse', () async {
      final dir = RemoteDir(_remoteDirHandle)..close();

      DirectoryProxy proxy = DirectoryProxy();
      dir.connect(openFlagDescribe, 0,
          InterfaceRequest(proxy.ctrl.request().passChannel()));

      await proxy.onOpen.first.then((response) {
        expect(response.s, ZX.ERR_NOT_SUPPORTED);
      }).catchError((err) async {
        fail(err.toString());
      });
      proxy.ctrl.close();
    });
  });
}

DirectoryProxy _serveLocal(PseudoDir dir) {
  DirectoryProxy proxy = DirectoryProxy();
  var status = dir.connect(openRightReadable | openRightWritable, 0,
      InterfaceRequest(proxy.ctrl.request().passChannel()));
  expect(status, ZX.OK);
  return proxy;
}

Future<DirectoryProxy> _openDirectory(DirectoryProxy dir, String path,
    [flags = openRightReadable]) async {
  DirectoryProxy proxy = DirectoryProxy();
  await dir.open(
      flags, 0, path, InterfaceRequest(proxy.ctrl.request().passChannel()));
  return proxy;
}

Future<String> _readFileContents(DirectoryProxy dir, String filename) async {
  final proxy = FileProxy();
  await dir.open(openRightReadable, modeTypeFile, filename,
      InterfaceRequest<Node>(proxy.ctrl.request().passChannel()));

  final data = await proxy.read(maxBuf);
  proxy.ctrl.close();
  return utf8.decode(data);
}

Future<String> _readFileContentsInRemoteDir(
    DirectoryProxy dir, String subdir, String filename) async {
  final proxy = await _openDirectory(dir, subdir);
  final contents = await _readFileContents(proxy, filename);
  proxy.ctrl.close();
  return contents;
}

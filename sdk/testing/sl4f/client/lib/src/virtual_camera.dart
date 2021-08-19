// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:core';

import 'package:logging/logging.dart';

import 'sl4f_client.dart';

final _log = Logger('virtual_camera');

class VirtualCameraSl4fException implements Exception {
  final String message;

  const VirtualCameraSl4fException(this.message);

  @override
  String toString() => 'VirtualCameraSl4fException: $message';
}

class VirtualCamera {
  final Sl4f _sl4f;
  const VirtualCamera(this._sl4f);

  /// Adds a stream config to the virtual device.
  ///
  /// By default, this will serve empty streams to the stream config. Otherwise,
  /// this function can be used to add a stream config.
  Future<void> addStreamConfig(int index, int width, int height) {
    _log.info('addStreamConfig($index, $width, $height)');

    try {
      return _sl4f.request('virtual_camera_facade.AddStreamConfig', {
        'index': index,
        'width': width,
        'height': height,
      });
    } on Exception catch (e) {
      throw VirtualCameraSl4fException(
          'addStreamConfig - Adding stream config with index ${index.toString()}, width ${width.toString()}, and height ${height.toString()} returned with error. Details: $e');
    }
  }

  /// Adds the config added using addStreamConfig to the device watcher.
  ///
  /// Once the virtual camera sources are set up and configured, this function
  /// will expose it to the DeviceWatcher, essentially putting the video stream
  /// in the device's view.
  Future<void> addToDeviceWatcher() {
    _log.info('addToDeviceWatcher()');

    try {
      return _sl4f.request('virtual_camera_facade.AddToDeviceWatcher', {});
    } on Exception catch (e) {
      throw VirtualCameraSl4fException(
          'addToDeviceWatcher - Adding to device watcher returned with error. Details: $e');
    }
  }
}

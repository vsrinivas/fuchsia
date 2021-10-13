// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';

import 'package:ermine/src/services/settings/task_service.dart';
import 'package:fidl_fuchsia_media/fidl_async.dart';
import 'package:fidl_fuchsia_media_audio/fidl_async.dart';
import 'package:flutter/material.dart';
import 'package:fuchsia_services/services.dart';

/// Defines a [TaskService] for updating and responding to volume.
///
/// Volume service runs all the time to allow changing volume even
/// when the shell ui is not visible.
class VolumeService implements TaskService {
  late final VoidCallback onChanged;

  VolumeControlProxy? _control;
  AudioCoreProxy? _audioCore;
  late StreamSubscription _volumeSubscription;

  late bool _muted;
  late double _volume;

  VolumeService();

  double get volume => _volume;
  set volume(double value) {
    // Setting new volume should only work when connected (started).
    assert(_control != null, 'VolumeService not started');

    if (_control != null && _volume != value) {
      _volume = value;
      _control!.setVolume(value);
      if (value == 0) {
        _muted = true;
        _control!.setMute(true);
      } else {
        _muted = false;
        _control!.setMute(false);
      }
    }
    onChanged();
  }

  // Increase volume by 10%.
  void increaseVolume() {
    volume = (volume + 0.1).clamp(0, 1);
  }

  // Decrease volume by 10%.
  void decreaseVolume() {
    volume = (volume - 0.1).clamp(0, 1);
  }

  IconData get icon => _muted ? Icons.volume_off : Icons.volume_up;

  bool get muted => _muted;
  set muted(bool value) {
    _muted = value;
    _control?.setMute(value);
    onChanged();
  }

  @override
  Future<void> start() async {
    if (_control != null) {
      return;
    }

    _control = VolumeControlProxy();
    _audioCore = AudioCoreProxy();
    Incoming.fromSvcPath().connectToService(_audioCore);

    await _audioCore!.bindUsageVolumeControl(
        Usage.withRenderUsage(AudioRenderUsage.media),
        _control!.ctrl.request());

    // Watch for changes in volume.
    _volumeSubscription =
        _control!.onVolumeMuteChanged.listen(_onVolumeMuteChanged);
  }

  @override
  Future<void> stop() async {}

  @override
  void dispose() {
    Future.wait(
      [_volumeSubscription.cancel()],
      cleanUp: (_) {
        _control?.ctrl.close();
        _control = null;
      },
    );
  }

  void _onVolumeMuteChanged(
      VolumeControl$OnVolumeMuteChanged$Response response) {
    _volume = response.newVolume;
    _muted = response.newMuted;
    onChanged();
  }
}

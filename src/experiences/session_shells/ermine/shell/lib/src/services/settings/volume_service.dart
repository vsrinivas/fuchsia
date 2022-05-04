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

  VolumeControlProxy? _controlMedia;
  VolumeControlProxy? _controlCommunication;
  AudioCoreProxy? _audioCore;
  late StreamSubscription _volumeSubscriptionMedia;
  late StreamSubscription _volumeSubscriptionCommunication;

  late bool _muted;
  late double _volume;

  VolumeService();

  double get volume => _volume;
  set volume(double value) {
    // Setting new volume should only work when connected (started).
    assert(_controlMedia != null,
        'VolumeService not started: media control null.');
    assert(_controlCommunication != null,
        'VolumeService not started: communication control null.');

    if (_controlMedia != null &&
        _controlCommunication != null &&
        _volume != value) {
      _volume = value;
      _controlMedia!.setVolume(value);
      _controlCommunication!.setVolume(value);
      if (value == 0) {
        _muted = true;
        _controlMedia!.setMute(true);
        _controlCommunication!.setMute(true);
      } else {
        _muted = false;
        _controlMedia!.setMute(false);
        _controlCommunication!.setMute(false);
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

  // Toggles mute for volume on/off.
  void toggleMute() {
    muted = !muted;
  }

  IconData get icon => _muted ? Icons.volume_off : Icons.volume_up;

  bool get muted => _muted;
  set muted(bool value) {
    _muted = value;
    _controlMedia?.setMute(value);
    _controlCommunication?.setMute(value);
    onChanged();
  }

  @override
  Future<void> start() async {
    if (_controlMedia != null || _controlCommunication != null) {
      return;
    }

    _controlMedia = VolumeControlProxy();
    _controlCommunication = VolumeControlProxy();
    _audioCore = AudioCoreProxy();
    Incoming.fromSvcPath().connectToService(_audioCore);

    await _audioCore!.bindUsageVolumeControl(
        Usage.withRenderUsage(AudioRenderUsage.media),
        _controlMedia!.ctrl.request());

    await _audioCore!.bindUsageVolumeControl(
        Usage.withRenderUsage(AudioRenderUsage.communication),
        _controlCommunication!.ctrl.request());

    // Watch for changes in volume.
    _volumeSubscriptionMedia =
        _controlMedia!.onVolumeMuteChanged.listen(_onVolumeMuteChanged);
    _volumeSubscriptionCommunication =
        _controlCommunication!.onVolumeMuteChanged.listen(_onVolumeMuteChanged);
  }

  @override
  Future<void> stop() async {}

  @override
  void dispose() {
    Future.wait(
      [
        _volumeSubscriptionMedia.cancel(),
        _volumeSubscriptionCommunication.cancel()
      ],
      cleanUp: (_) {
        _controlMedia?.ctrl.close();
        _controlMedia = null;
        _controlCommunication?.ctrl.close();
        _controlCommunication = null;
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

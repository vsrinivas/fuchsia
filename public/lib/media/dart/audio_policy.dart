// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:apps.media.services/audio_policy_service.fidl.dart';
import 'package:apps.media.services/audio_renderer.fidl.dart';
import 'package:application.lib.app.dart/app.dart';
import 'package:application.services/service_provider.fidl.dart';

/// Type for |AudioPolicy| update callbacks.
typedef void UpdateCallback();

/// System audio policy.
class AudioPolicy {
  static const double _minLevelGain = -60.0;
  static const double _unityGain = 0.0;

  final AudioPolicyServiceProxy _audioPolicyService =
      new AudioPolicyServiceProxy();

  double _systemAudioGainDb = -12.0;
  bool _systemAudioMuted = false;

  /// Constructs a AudioPolicy object.
  AudioPolicy(ServiceProvider services) {
    connectToService(services, _audioPolicyService.ctrl);
    _handleServiceUpdates(AudioPolicyService.kInitialStatus, null);
  }

  /// Called when properties have changed.
  UpdateCallback updateCallback;

  /// Disposes this object.
  void dispose() {
    _audioPolicyService.ctrl.close();
  }

  /// Gets the system-wide audio gain in db.
  double get systemAudioGainDb => _systemAudioGainDb;

  /// Sets the system-wide audio gain in db.
  set systemAudioGainDb(double value) {
    _audioPolicyService.setSystemAudioGain(value);
  }

  /// Gets system-wide audio muted state.
  bool get systemAudioMuted => _systemAudioMuted;

  /// Sets system-wide audio muted state.
  set systemAudioMuted(bool value) {
    _audioPolicyService.setSystemAudioMute(value);
  }

  /// Gets the perceived system-wide audio level in the range [0,1].
  double get systemAudioPerceivedLevel => gainToLevel(_systemAudioGainDb);

  /// Sets the perceived system-wide audio level in the range [0,1].
  set systemAudioPerceivedLevel(double value) {
    _audioPolicyService.setSystemAudioGain(levelToGain(value));
  }

  // Handles a status update from the audio policy service. Call with
  // kInitialStatus, null to initiate status updates.
  void _handleServiceUpdates(int version, AudioPolicyStatus status) {
    if (status != null) {
      _systemAudioGainDb = status.systemAudioGainDb;
      _systemAudioMuted = status.systemAudioMuted;

      if (updateCallback != null) {
        updateCallback();
      }
    }

    _audioPolicyService.getStatus(version, _handleServiceUpdates);
  }

  /// Converts a gain in db to an audio 'level' in the range 0.0 to 1.0
  /// inclusive.
  static double gainToLevel(double gain) {
    if (gain <= _minLevelGain) {
      return 0.0;
    }

    if (gain >= _unityGain) {
      return 1.0;
    }

    return 1.0 - gain / _minLevelGain;
  }

  /// Converts an audio 'level' in the range 0.0 to 1.0 inclusive to a gain in
  /// db.
  static double levelToGain(double level) {
    if (level <= 0.0) {
      return AudioRenderer.kMutedGain;
    }

    if (level >= 1.0) {
      return _unityGain;
    }

    return (1.0 - level) * _minLevelGain;
  }
}

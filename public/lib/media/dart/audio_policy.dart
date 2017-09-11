// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:lib.media.fidl/audio_policy_service.fidl.dart';
import 'package:lib.media.fidl/audio_renderer.fidl.dart';
import 'package:lib.app.dart/app.dart';
import 'package:lib.app.fidl/service_provider.fidl.dart';

/// Type for |AudioPolicy| update callbacks.
typedef void UpdateCallback();

/// System audio policy.
class AudioPolicy {
  static const double _minLevelGain = -60.0;
  static const double _unityGain = 0.0;
  static const double _initialGain = -12.0;

  // These values determine what counts as a 'significant' change when deciding
  // whether to call |updateCallback|.
  static const double _minDbDiff = 0.006;
  static const double _minPerceivedDiff = 0.0001;

  final AudioPolicyServiceProxy _audioPolicyService =
      new AudioPolicyServiceProxy();

  double _systemAudioGainDb = _initialGain;
  bool _systemAudioMuted = false;
  double _systemAudioPerceivedLevel = gainToLevel(_initialGain);

  /// Constructs a AudioPolicy object.
  AudioPolicy(ServiceProvider services) {
    connectToService(services, _audioPolicyService.ctrl);
    _handleServiceUpdates(AudioPolicyService.kInitialStatus, null);
  }

  /// Called when properties have changed significantly.
  UpdateCallback updateCallback;

  /// Disposes this object.
  void dispose() {
    _audioPolicyService.ctrl.close();
  }

  /// Gets the system-wide audio gain in decibels. Gain values are in the range
  /// -160db to 0db inclusive.
  double get systemAudioGainDb => _systemAudioGainDb;

  /// Sets the system-wide audio gain in db. |value| is clamped to the range
  /// -160db to 0db inclusive. When gain is set to -160db, |systemAudioMuted| is
  /// implicitly set to true. When gain is changed from -160db to a higher
  /// value, |systemAudioMuted| is implicitly set to false.
  set systemAudioGainDb(double value) {
    value = value.clamp(AudioRenderer.kMutedGain, _unityGain);
    if (_systemAudioGainDb == value) {
      return;
    }

    _systemAudioGainDb = value;
    _systemAudioPerceivedLevel = gainToLevel(value);

    if (_systemAudioGainDb == AudioRenderer.kMutedGain) {
      _systemAudioMuted = true;
    }

    _audioPolicyService.setSystemAudioGain(_systemAudioGainDb);
  }

  /// Gets system-wide audio muted state. |systemAudioMuted| is always true
  /// when |systemAudioGainDb| is -160db.
  bool get systemAudioMuted => _systemAudioMuted;

  /// Sets system-wide audio muted state. Setting this value to false when
  /// |systemAudioGainDb| is -160db has no effect.
  set systemAudioMuted(bool value) {
    value = value || _systemAudioGainDb == AudioRenderer.kMutedGain;
    if (_systemAudioMuted == value) {
      return;
    }

    _systemAudioMuted = value;
    _audioPolicyService.setSystemAudioMute(_systemAudioMuted);
  }

  /// Gets the perceived system-wide audio level in the range [0,1]. This value
  /// is intended to be used for volume sliders. If there is no separate mute
  /// control, use (systemAudioMuted ? 0.0 : systemAudioPerceivedLevel).
  double get systemAudioPerceivedLevel => _systemAudioPerceivedLevel;

  /// Sets the perceived system-wide audio level in the range [0,1]. When this
  /// property is set to 0.0, |systemAudioGainDb| is set to -160db and
  /// |systemAudioMuted| is implicitly set to true.
  set systemAudioPerceivedLevel(double value) {
    _systemAudioPerceivedLevel = value.clamp(0.0, 1.0);
    _systemAudioGainDb = levelToGain(_systemAudioPerceivedLevel);
    _audioPolicyService.setSystemAudioGain(_systemAudioGainDb);
  }

  // Handles a status update from the audio policy service. Call with
  // kInitialStatus, null to initiate status updates.
  void _handleServiceUpdates(int version, AudioPolicyStatus status) {
    if (status != null) {
      bool callUpdate = _systemAudioMuted != status.systemAudioMuted ||
          (_systemAudioGainDb - status.systemAudioGainDb).abs() > _minDbDiff;

      _systemAudioGainDb = status.systemAudioGainDb;
      _systemAudioMuted = status.systemAudioMuted;

      double newPerceivedLevel = gainToLevel(_systemAudioGainDb);
      if ((_systemAudioPerceivedLevel - newPerceivedLevel).abs() >
          _minPerceivedDiff) {
        _systemAudioPerceivedLevel = newPerceivedLevel;
        callUpdate = true;
      }

      if (callUpdate && updateCallback != null) {
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

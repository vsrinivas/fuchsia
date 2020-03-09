// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'sl4f_client.dart';

/// This is a wrapper for the Manager and ChannelControl protocol in the fuchsia.update FIDL
/// interface.
class Update {
  final Sl4f _sl4f;

  Update(this._sl4f);

  /// Get the current state of the update manager.
  Future<State> getState() async {
    final result = await _sl4f.request('update_facade.GetState');
    final String enumString = 'ManagerState.${result['state']}'.toLowerCase();
    final managerState = ManagerState.values
        .firstWhere((e) => e.toString().toLowerCase() == enumString);
    return State()
      ..state = managerState
      ..versionAvailable = result['version_available'];
  }

  /// Immediately check for an update.
  Future<CheckStartedResult> checkNow({bool serviceInitiated}) async {
    final result = await _sl4f.request(
        'update_facade.CheckNow',
        serviceInitiated != null
            ? {'service-initiated': serviceInitiated}
            : null);
    final String enumString =
        'CheckStartedResult.${result['check_started']}'.toLowerCase();
    return CheckStartedResult.values
        .firstWhere((e) => e.toString().toLowerCase() == enumString);
  }

  Future<String> getCurrentChannel() async =>
      await _sl4f.request('update_facade.GetCurrentChannel');

  Future<String> getTargetChannel() async =>
      await _sl4f.request('update_facade.GetTargetChannel');

  Future<void> setTargetChannel(String channel) =>
      _sl4f.request('update_facade.SetTargetChannel', {'channel': channel});

  Future<List<String>> getChannelList() async {
    final List<dynamic> channelList =
        await _sl4f.request('update_facade.GetChannelList');
    return channelList.cast<String>();
  }
}

class State {
  ManagerState state;
  String versionAvailable;
}

enum ManagerState {
  idle,
  checkingForUpdates,
  updateAvailable,
  performingUpdate,
  waitingForReboot,
  finalizingUpdate,
  encounteredError,
}

enum CheckStartedResult {
  started,
  inProgress,
  throttled,
}

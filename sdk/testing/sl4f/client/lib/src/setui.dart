// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:logging/logging.dart';

import 'sl4f_client.dart';

enum LoginOverride { none, autologinGuest, authProvider }
enum NetworkOption { wifi, ethernet, unknown }

final _overrideToJson = {
  LoginOverride.none: 'none',
  LoginOverride.autologinGuest: 'autologin_guest',
  LoginOverride.authProvider: 'auth_provider',
};

final _networkToJson = {
  NetworkOption.wifi: 'wifi',
  NetworkOption.ethernet: 'ethernet',
  NetworkOption.unknown: 'unknown',
};

final _log = Logger('setui_sl4f');

class SetUiSl4fException implements Exception {
  final String message;

  SetUiSl4fException(this.message);

  @override
  String toString() => 'SetUiSl4fException: $message';
}

class SetUi {
  final Sl4f _sl4f;

  SetUi(this._sl4f);

  Future<void> mutateLoginOverride(LoginOverride override) =>
      _sl4f.request('setui_facade.Mutate', {
        'account': {
          'operation': 'set_login_override',
          'login_override': _overrideToJson[override],
        }
      });

  Future<void> setDevNetworkOption(NetworkOption option) async {
    _log.info('Setting Dev network option to ${option.toString()}');
    if (option == NetworkOption.unknown) {
      throw SetUiSl4fException(
          'Invalid Dev Options input. Accepted are wifi and ethernet.');
    }
    await _sl4f.request('setui_facade.SetNetwork', _networkToJson[option]);
  }

  Future<NetworkOption> getDevNetworkOption() async {
    final result = await _sl4f.request('setui_facade.GetNetwork', null);
    switch (result) {
      case 'wifi':
        return NetworkOption.wifi;
      case 'ethernet':
        return NetworkOption.ethernet;
      default:
        return NetworkOption.unknown;
    }
  }
}

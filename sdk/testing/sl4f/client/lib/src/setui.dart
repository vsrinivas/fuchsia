// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'sl4f_client.dart';

enum LoginOverride {
  none,
  autologinGuest,
  authProvider
}

final _overrideToJson = {
  LoginOverride.none: 'none',
  LoginOverride.autologinGuest: 'autologin_guest',
  LoginOverride.authProvider: 'auth_provider',
};

class SetUi {
  final Sl4f _sl4f;

  SetUi(this._sl4f);

  Future<void> mutateLoginOverride(LoginOverride override) => _sl4f.request(
      'setui_facade.Mutate', {
      'account': {
        'operation': 'set_login_override',
        'login_override': _overrideToJson[override],
      }
    });
}

// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:apps.modular.services.device/device_context.fidl.dart';
import 'package:apps.modular.services.device/user_provider.fidl.dart';
import 'package:lib.widgets/model.dart';

export 'package:lib.widgets/model.dart' show ScopedModel, ScopedModelDescendant;

class DeviceShellFactoryModel extends Model {
  DeviceContext _deviceContext;
  UserProvider _userProvider;
  List<String> _users;

  DeviceContext get deviceContext => _deviceContext;
  UserProvider get userProvider => _userProvider;
  List<String> get users => _users;

  set deviceContext(DeviceContext deviceContext) {
    _deviceContext = deviceContext;
    notifyListeners();
  }

  set userProvider(UserProvider userProvider) {
    _userProvider = userProvider;
    _loadUsers();
    notifyListeners();
  }

  void _loadUsers() {
    _userProvider.previousUsers((List<String> users) {
      _users = users;
      notifyListeners();
    });
  }

  void onLogout() {
    _users = null;
    _loadUsers();
    notifyListeners();
  }
}

// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:apps.modular.services.device/user_provider.fidl.dart';
import 'package:lib.fidl.dart/bindings.dart';
import 'package:flutter/widgets.dart';

class UserWatcherImpl extends UserWatcher {
  final UserWatcherBinding _binding = new UserWatcherBinding();
  final VoidCallback onUserLogout;

  UserWatcherImpl({this.onUserLogout});

  InterfaceHandle<UserWatcher> getHandle() => _binding.wrap(this);

  @override
  void onLogout() {
    onUserLogout?.call();
  }

  void close() => _binding.close();
}

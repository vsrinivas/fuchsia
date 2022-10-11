// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';
import 'dart:ui';

import 'package:ermine/src/services/settings/task_service.dart';
import 'package:fidl_fuchsia_net_interfaces/fidl_async.dart';
import 'package:fuchsia_services/services.dart';

/// Defines a [TaskService] to detect change in network interfaces.
///
/// When a change to network interfaces is detected, calls the [onChanged]
/// callback. The client can use dart.io [NetworkInterface.list()] method to
/// get the list of all network interfaces on device.
class NetworkAddressService implements TaskService {
  late final VoidCallback onChanged;

  StateProxy? _proxy;
  WatcherProxy? _watcher;
  StreamSubscription<Event>? _subscription;

  @override
  Future<void> start() async {
    _proxy = StateProxy();
    _watcher = WatcherProxy();
    Incoming.fromSvcPath().connectToService(_proxy);
    await _proxy!.getWatcher(WatcherOptions(), _watcher!.ctrl.request());
    _subscription = _watcher!.watch().asStream().listen((_) => _watch());
  }

  void _watch() {
    onChanged();
    _subscription?.cancel();
    _subscription = _watcher!.watch().asStream().listen((_) => _watch());
  }

  @override
  Future<void> stop() async {
    dispose();
  }

  @override
  void dispose() {
    _subscription?.cancel();
    _watcher?.ctrl.close();
    _proxy?.ctrl.close();
  }
}

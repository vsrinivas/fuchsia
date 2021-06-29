// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:math';
import 'dart:ui';

import 'package:ermine/src/services/settings/task_service.dart';
import 'package:fidl_fuchsia_memory/fidl_async.dart';
import 'package:fuchsia_services/services.dart';

/// Defines a [TaskService] to watch for memory usage in the system.
class MemoryWatcherService extends Watcher implements TaskService {
  late final VoidCallback onChanged;

  MonitorProxy? _proxy;
  WatcherBinding? _binding;

  @override
  Future<void> start() async {
    _proxy = MonitorProxy();
    Incoming.fromSvcPath().connectToService(_proxy);
    _binding = WatcherBinding();
    await _proxy!.watch(_binding!.wrap(this));
  }

  @override
  Future<void> stop() async {
    dispose();
  }

  @override
  void dispose() {
    _binding?.close();
    _proxy?.ctrl.close();
  }

  /// Returns the total amount of memory in the system.
  double? get memTotal =>
      _stats == null ? null : _bytesToGB(_stats!.totalBytes);

  /// Returns the amount of memory currently in use in the system.
  double? get memUsed => _stats == null
      ? null
      : _bytesToGB(_stats!.totalBytes - _stats!.freeBytes);

  Stats? _stats;

  @override
  Future<void> onChange(Stats stats) async {
    _stats = stats;
    onChanged();
  }

  double _bytesToGB(int bytes) {
    return (bytes / pow(1024, 3));
  }
}

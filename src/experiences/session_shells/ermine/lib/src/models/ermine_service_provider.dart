// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter/material.dart';

import 'package:fidl/fidl.dart' show AsyncBinding, InterfaceRequest, AsyncProxy;
import 'package:fidl_fuchsia_sys/fidl_async.dart' show ServiceProvider;
import 'package:fuchsia_logger/logger.dart';
import 'package:zircon/zircon.dart';

/// Implements ServiceProvider for Ermine.
///
/// Advertises services available on Ermine to components launched from it.
class ErmineServiceProvider extends ServiceProvider {
  final Map<String, ValueChanged<Channel>> _bindingsInfo = {};
  final List<AsyncBinding> _bindings = [];

  void advertise<T>({String name, AsyncProxy service, AsyncBinding binding}) {
    _bindingsInfo[name] = (channel) {
      _bindings.add(binding..bind(service, InterfaceRequest<T>(channel)));
    };
  }

  // ServiceProvider.
  @override
  Future<void> connectToService(String serviceName, Channel channel) async {
    if (_bindingsInfo.containsKey(serviceName)) {
      _bindingsInfo[serviceName](channel);
    } else {
      log.warning(
          'ErmineServiceProvider: received request for unknown service: $serviceName !');
      channel.close();
    }
  }
}

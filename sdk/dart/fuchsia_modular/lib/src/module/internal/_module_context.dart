// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:fidl_fuchsia_modular/fidl_async.dart' as fidl;
import 'package:fuchsia_services/services.dart';

final fidl.ModuleContext _moduleContext = () {
  final proxy = fidl.ModuleContextProxy();
  Incoming.fromSvcPath()
    ..connectToService(proxy)
    ..close();
  return proxy;
}();

/// Method to return the singleton, connected [fidl.ModuleCOntext].
/// Multiple calls to this method will result in the same
/// context being returned.
fidl.ModuleContext getModuleContext() => _moduleContext;

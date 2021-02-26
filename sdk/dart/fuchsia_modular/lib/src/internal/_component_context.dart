// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:fidl_fuchsia_modular/fidl_async.dart' as fidl;
import 'package:fuchsia_services/services.dart';

final fidl.ComponentContextProxy _componentContextProxy = () {
  final proxy = fidl.ComponentContextProxy();
  Incoming.fromSvcPath()
    ..connectToService(proxy)
    ..close();
  return proxy;
}();

/// Return the [ComponentContext] cached instance associated with the
/// currently running component.
fidl.ComponentContext getComponentContext() => _componentContextProxy;

// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:apps.maxwell.services.context/context_reader.fidl.dart';
import 'package:lib.fidl.dart/bindings.dart';

/// Signature for callbacks that handle context updates.
typedef void UpdateCallback(ContextUpdate value);

/// Functional wrapper class for [ContextListener], using callbacks to
/// implement interface methods.
class ContextListenerImpl extends ContextListener {
  final _binding = new ContextListenerBinding();
  final UpdateCallback _onUpdate;

  ContextListenerImpl(this._onUpdate);

  /// Gets the [InterfaceHandle] for this [ContextListener]
  /// implementation. The returned handle should only be used once.
  InterfaceHandle<ContextListener> getHandle() => _binding.wrap(this);

  @override
  void onContextUpdate(ContextUpdate update) => _onUpdate(update);

  void close() => _binding.close();
}

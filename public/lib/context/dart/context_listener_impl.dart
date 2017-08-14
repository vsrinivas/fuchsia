// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:apps.maxwell.services.context/context_reader.fidl.dart';
import 'package:lib.fidl.dart/bindings.dart';

/// Signature for callbacks that handle context updates.
typedef void UpdateCallback(ContextUpdateForTopics value);

/// Functional wrapper class for [ContextListenerForTopics], using callbacks to
/// implement interface methods.
class ContextListenerForTopicsImpl extends ContextListenerForTopics {
  final _binding = new ContextListenerForTopicsBinding();
  final UpdateCallback _onUpdate;

  ContextListenerForTopicsImpl(this._onUpdate);

  /// Gets the [InterfaceHandle] for this [ContextListenerForTopics]
  /// implementation. The returned handle should only be used once.
  InterfaceHandle<ContextListenerForTopics> getHandle() => _binding.wrap(this);

  @override
  void onUpdate(ContextUpdateForTopics update) => _onUpdate(update);

  void close() => _binding.close();
}

// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:apps.maxwell.services.context/publisher_link.fidl.dart';
import 'package:lib.fidl.dart/bindings.dart';

typedef void VoidCallback();

/// Functional wrapper class for [ContextPublisherController], using callbacks
/// to implement interface methods.
class ContextPublisherControllerImpl extends ContextPublisherController {
  final _binding = new ContextPublisherControllerBinding();
  final VoidCallback _onHasSubscribers;
  final VoidCallback _onNoSubscribers;

  ContextPublisherControllerImpl(this._onHasSubscribers, this._onNoSubscribers);

  // Use { } rather than => with dynamic Functions as the non-web Dart VM will
  // error if void is expected but, for example, the callback is async and
  // returns a Future.

  @override
  void onHasSubscribers() => _onHasSubscribers?.call();

  @override
  void onNoSubscribers() => _onNoSubscribers?.call();

  /// Gets the [InterfaceHandle] for this [ContextPublisherController]
  /// implementation.
  ///
  /// The returned handle should only be used once.
  InterfaceHandle<ContextPublisherController> getHandle() =>
      _binding.wrap(this);

  void close() => _binding.close();
}

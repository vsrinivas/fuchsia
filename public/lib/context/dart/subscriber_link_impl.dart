// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:apps.maxwell.services.context/subscriber_link.fidl.dart';
import 'package:lib.fidl.dart/bindings.dart';

/// Signature for callbacks that handle context updates.
typedef void UpdateCallback(ContextUpdate value);

class ContextSubscriberLinkImpl extends ContextSubscriberLink {
  final _binding = new ContextSubscriberLinkBinding();
  final UpdateCallback _onUpdate;

  ContextSubscriberLinkImpl(this._onUpdate);

  /// Gets the [InterfaceHandle] for this [ContextSubscriberLink]
  /// implementation. The returned handle should only be used once.
  InterfaceHandle<ContextSubscriberLink> getHandle() => _binding.wrap(this);

  @override
  void onUpdate(ContextUpdate update) => _onUpdate(update);

  void close() => _binding.close();
}

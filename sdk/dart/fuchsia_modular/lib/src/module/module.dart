// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'internal/_module_impl.dart';

/// The [Module] class provides a mechanism for module authors
/// to interact with the underlying framework. The main responsibilities
/// of the [Module] class are to implement the intent handler
/// interface and the lifecycle interface.
abstract class Module {
  static Module? _module;

  /// returns a shared instance of this.
  factory Module() {
    return _module ??= ModuleImpl();
  }

  /// When [RemoveSelfFromStory()] is called the framework will stop the
  /// module and remove it from the story. If there are no more running modules
  /// in the story the story will be stopped.
  void removeSelfFromStory();
}

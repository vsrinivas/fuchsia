// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';

import 'package:fidl/fidl.dart';
import 'package:fidl_fuchsia_services_examples/fidl_async.dart';

class ThoughtLeakerImpl extends ThoughtLeaker {
  /// The discoverable name for this service
  static const String serviceName = ThoughtLeaker.$serviceName;

  final String _currentThought;

  final _binding = ThoughtLeakerBinding();

  /// Creates an instance of this with a given thought which will
  /// be exposed to the bound request.
  ThoughtLeakerImpl(this._currentThought);

  /// Calling this method will bind this implemenation to the [request].
  ///
  /// When a request is bound to an interface the messages sent over the bound
  /// channel will be proxied to this implementation. Calling this method
  /// multiple times will throw an exception.
  void bind(InterfaceRequest<ThoughtLeaker> request) =>
      _binding.bind(this, request);

  @override
  Future<String> currentThought() async => _currentThought;
}

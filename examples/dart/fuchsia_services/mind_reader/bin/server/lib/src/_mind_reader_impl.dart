// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(https://fxbug.dev/84961): Fix null safety and remove this language version.
// @dart=2.9

import 'dart:async';

import 'package:fidl/fidl.dart';
import 'package:fidl_fuchsia_services_examples/fidl_async.dart';
import 'package:fuchsia_services/services.dart';

/// The [MindReaderImpl] extends the MindReader class and provides
/// concrete implementations for fidl defined methods.
class MindReaderImpl extends MindReader {
  /// The discoverable name for this service
  static const String serviceName = MindReader.$serviceName;

  final _binding = MindReaderBinding();

  /// Calling this method will bind this implemenation to the [request].
  ///
  /// When a request is bound to an interface the messages sent over the bound
  /// channel will be proxied to this implementation. Calling this method
  /// multiple times will throw an exception.
  void bind(InterfaceRequest<MindReader> request) =>
      _binding.bind(this, request);

  @override
  Future<String> readMind() async {
    final proxy = ThoughtLeakerProxy();
    // Attempt to connect to the ThoughtLeaker service which has been exposed to
    // us as an incoming service in the in/svc directory. Note: for this to
    // succeed the service must have been explicity exposed to us by the parent
    // process and we must identify that we can connect to it in our component
    // manifest file.
    final incoming = Incoming.fromSvcPath()
      ..connectToService<ThoughtLeaker>(proxy);

    final response = await _attemptReadMind(proxy);
    proxy.ctrl.close();
    await incoming.close();

    return response;
  }

  Future<String> _attemptReadMind(ThoughtLeaker thoughtLeaker) async {
    try {
      // If the service is exposed and has a valid connection we return
      // the response that is read from the [ThoughtLeaker] instance
      final thought = await thoughtLeaker.currentThought();
      return 'You are thinking: "$thought"';
    } on Exception catch (_) {
      // The service was not exposed to us so we cannot read the
      // caller's mind.
      return 'I have no idea what you are thinking.\n'
          'Make sure you expose the ThoughtLeaker service '
          'if you want your mind read.';
    }
  }
}

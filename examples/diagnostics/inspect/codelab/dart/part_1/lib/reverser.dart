// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(https://fxbug.dev/84961): Fix null safety and remove this language version.
// @dart=2.9

import 'dart:async';

import 'package:fidl/fidl.dart';
import 'package:fidl_fuchsia_examples_inspect/fidl_async.dart' as fidl_codelab;
import 'package:meta/meta.dart';

// [START reverser_impl]
typedef BindCallback = void Function(InterfaceRequest<fidl_codelab.Reverser>);
typedef VoidCallback = void Function();

class ReverserImpl extends fidl_codelab.Reverser {
  final _binding = fidl_codelab.ReverserBinding();

  // CODELAB: Create a constructor that takes an Inspect node.
  ReverserImpl();

  @override
  Future<String> reverse(String value) async {
    // CODELAB: Add stats about incoming requests.
    print(String.fromCharCodes(value.runes.toList().reversed));
    await Future.delayed(Duration(hours: 10));
    return '';
  }

  static final _bindingSet = <ReverserImpl>{};
  static BindCallback getDefaultBinder() {
    return (InterfaceRequest<fidl_codelab.Reverser> request) {
      // CODELAB: Add stats about incoming connections.
      final reverser = ReverserImpl()..bind(request, onClose: () {});
      _bindingSet.add(reverser);
    };
  }

  void bind(
    InterfaceRequest<fidl_codelab.Reverser> request, {
    @required VoidCallback onClose,
  }) {
    _binding.stateChanges.listen((state) {
      if (state == InterfaceState.closed) {
        dispose();
        onClose();
      }
    });
    _binding.bind(this, request);
  }

  void dispose() {}
}
// [END reverser_impl]

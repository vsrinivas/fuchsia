// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(https://fxbug.dev/84961): Fix null safety and remove this language version.
// @dart=2.9

import 'dart:async';
import 'dart:collection';

import 'package:fidl/fidl.dart';
import 'package:fidl_fuchsia_examples_inspect/fidl_async.dart' as fidl_codelab;
// [START part_1_import]
import 'package:fuchsia_inspect/inspect.dart' as inspect;
// [END part_1_import]
import 'package:meta/meta.dart';

typedef BindCallback = void Function(InterfaceRequest<fidl_codelab.Reverser>);
typedef VoidCallback = void Function();

class ReverserStats {
  inspect.Node node;
  inspect.IntProperty globalRequestCount;

  ReverserStats(this.node, this.globalRequestCount) {
    node.intProperty('request_count').setValue(0);
    node.intProperty('response_count').setValue(0);
  }

  ReverserStats.noop() {
    node = inspect.Node.deleted();
    globalRequestCount = inspect.IntProperty.deleted();
  }

  inspect.IntProperty get requestCount => node.intProperty('request_count');
  inspect.IntProperty get responseCount => node.intProperty('response_count');

  void dispose() {
    node.delete();
  }
}

class ReverserImpl extends fidl_codelab.Reverser {
  final _binding = fidl_codelab.ReverserBinding();
  final ReverserStats stats;

  ReverserImpl(this.stats);

  @override
  Future<String> reverse(String value) async {
    stats.globalRequestCount.add(1);
    stats.requestCount.add(1);
    // [START part_1_result]
    final result = String.fromCharCodes(value.runes.toList().reversed);
    // [START_EXCLUDE]
    stats.responseCount.add(1);
    // [END_EXCLUDE]
    return result;
    // [END part_1_result]
  }

  static final _reversers = HashMap<String, ReverserImpl>();
  // [START part_1_add_connection_count]
  // [START part_1_update_reverser]
  static BindCallback getDefaultBinder(inspect.Node node) {
    // [END part_1_update_reverser]
    // [START_EXCLUDE]
    final globalRequestCount = node.intProperty('total_requests')..setValue(0);
    // [END_EXCLUDE]
    final glabalConnectionCount = node.intProperty('connection_count')
      ..setValue(0);
    return (InterfaceRequest<fidl_codelab.Reverser> request) {
      glabalConnectionCount.add(1);
      // [END part_1_add_connection_count]
      // [START part_1_connection_child]
      final name = inspect.uniqueName('connection');
      // [END part_1_connection_child]
      final child = node.child(name);
      final stats = ReverserStats(child, globalRequestCount);
      final reverser = ReverserImpl(stats)
        ..bind(request, onClose: () {
          _reversers.remove(name);
        });
      _reversers[name] = reverser;
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

  void dispose() {
    stats.dispose();
  }
}

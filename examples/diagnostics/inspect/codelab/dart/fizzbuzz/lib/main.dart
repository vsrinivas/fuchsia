// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(https://fxbug.dev/84961): Fix null safety and remove this language version.
// @dart=2.9

import 'dart:async';

import 'package:fidl/fidl.dart';
import 'package:fidl_fuchsia_examples_inspect/fidl_async.dart' as fidl_codelab;
import 'package:fuchsia_inspect/inspect.dart' as inspect;
import 'package:fuchsia_logger/logger.dart';
import 'package:fuchsia_services/services.dart';

class _FizzBuzzImpl extends fidl_codelab.FizzBuzz {
  final Set<fidl_codelab.FizzBuzzBinding> _bindingSet = {};
  final inspect.Node _node;

  _FizzBuzzImpl(this._node) {
    _node.intProperty('incoming_connection_count').setValue(0);
    _node.intProperty('closed_connection_count').setValue(0);
    _node.intProperty('request_count').setValue(0);
    // Unlike Rust and C++, Dart inspect doesn't support exponential histograms.
    // TODO(fxb/44725): when exponential histograms are added create an
    // exponential histogram named "request_time_histogram_us", with the
    // floor=1, initial_step=2, step_multiplier=2, buckets=16.
  }

  void bind(InterfaceRequest<fidl_codelab.FizzBuzz> request) {
    _node.intProperty('incoming_connection_count').add(1);
    var binding = fidl_codelab.FizzBuzzBinding();
    binding.stateChanges.listen((state) {
      if (state == InterfaceState.closed) {
        _node.intProperty('closed_connection_count').add(1);
        _bindingSet.remove(binding);
      }
    });
    binding.bind(this, request);
    _bindingSet.add(binding);
  }

  @override
  Future<String> execute(int count) async {
    _node.intProperty('request_count').add(1);
    var output = '';
    for (var i = 1; i <= count; i++) {
      if (i != 1) {
        output += ' ';
      }
      if (i % 3 == 0) {
        output += 'Fizz';
      }
      if (i % 5 == 0) {
        output += 'Buzz';
      }
      if (i % 3 != 0 && i % 5 != 0) {
        output += i.toString();
      }
    }
    return output;
  }
}

void main(List<String> args) {
  final context = ComponentContext.create();

  setupLogger(name: 'inspect_dart_codelab', globalTags: ['fizzbuzz']);

  log.info('starting up...');

  final inspectNode = (inspect.Inspect()..serve(context.outgoing)).root;
  final fizzbuzz = _FizzBuzzImpl(inspectNode);

  // We need to call serveFromStartupInfo from outgoing after we add all public
  // services. If there's no public services being exposed, we can use
  // ComponentContext.createAndServe() for convenience.
  context.outgoing
    ..addPublicService<fidl_codelab.FizzBuzz>(
        fizzbuzz.bind, fidl_codelab.FizzBuzz.$serviceName)
    ..serveFromStartupInfo();
}

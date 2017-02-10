// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';
import 'dart:convert';

import 'package:apps.modular.services.story/link.fidl.dart';
import 'package:lib.fidl.dart/bindings.dart';

void _log(String msg) {
  print('[Todo Story Example] $msg');
}

class TodoListData extends LinkWatcher {
  final LinkWatcherBinding _binding = new LinkWatcherBinding();
  final Future<LinkProxy> _link;
  Function _callback;

  TodoListData(Future<LinkProxy> link) : _link = link {
    _setup();
  }

  void setCallback(void callback(List<String> entries)) {
    this._callback = callback;
  }

  Future _setup() async {
    LinkProxy link = await _link;
    link.ctrl.error.then((ProxyError error) {
      _log("Error on link " + error.toString());
    });
    link.watchAll(_binding.wrap(this));
  }

  @override
  void notify(String json) {
    _log("Received " + json);
    _parse(json);
  }

  void _parse(String json) {
    if (_callback == null) {
      return;
    }
    final decoded = JSON.decode(json);
    if (decoded == null) {
      _callback(new List<String>());
      return;
    }
    if (decoded is! Map || decoded["list"] is! List) {
      _log("decoded is not of the right type");
      return;
    }
    _callback(decoded["list"]);
  }

  Future setNewList(List<String> items) async {
    final encoded = JSON.encode(items);
    _log("Storing " + encoded);
    // TODO(etiennej): Storing the items as a simple list may not be the best
    // representation, as any concurrent change will create a conflict.
    (await _link).set(["list"], encoded);
  }
}

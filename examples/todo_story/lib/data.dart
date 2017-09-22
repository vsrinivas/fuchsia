// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';
import 'dart:convert';

import 'package:lib.story.fidl/link.fidl.dart';
import 'package:lib.fidl.dart/bindings.dart';

void _log(String msg) {
  print('[Todo Story Example] $msg');
}

/// A single Todo item.
class TodoItem {
  final String text;
  final int position;

  const TodoItem({this.text, this.position});
}

typedef void LinkConnectorCallback(List<TodoItem> entries);

/// [LinkConnector] maintains an updated view of a list of [TodoItem] objects
/// from a provided [LinkProxy]. [LinkConnector] can also be used to modify
/// this list.
///
/// The [TodoItem]s are stored inside a link using the following scheme:
/// {
///   "items": [
///     "todo 1",
///     "todo 2",
///     "todo 3"
///   ]
/// }
class LinkConnector extends LinkWatcher {
  final LinkWatcherBinding _binding = new LinkWatcherBinding();
  final Future<LinkProxy> _link;
  List<TodoItem> _items = <TodoItem>[];

  LinkConnectorCallback _callback;

  LinkConnector(this._link) {
    _setup();
  }

  /// Sets the callback that will be called for each change of the todo list.
  /// [callback] will be immediately called with the current state of the todo
  /// list.
  void setCallback(LinkConnectorCallback callback) {
    this._callback = callback;
    Timer.run(() {
      _callCallback();
    });
  }

  Future _setup() async {
    LinkProxy link = await _link;
    // ignore: unawaited_futures
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

  Future _parse(String json) async {
    final dynamic decoded = JSON.decode(json);
    if (decoded == null) {
      _items = <TodoItem>[];
      _callCallback();
      return;
    }
    if (decoded is! Map || decoded["items"] is! List) {
      _log("decoded is not of the right type");
      return;
    }
    _items = <TodoItem>[];
    for (int i = 0; i < decoded["items"].length; i++) {
      _items.add(new TodoItem(text: decoded["items"][i], position: i));
    }
    _callCallback();
  }

  void _callCallback() {
    if (_callback != null) {
      _callback(_items);
    }
  }

  /// Adds a new todo item with the provided text at the end of the current
  /// list.
  Future addItem(String text) async {
    int position = _items.length;
    TodoItem item = new TodoItem(text: text, position: position);
    _items.add(item);
    return _setLink();
  }

  /// Removes the provided [TodoItem] from the current list.
  Future removeItem(TodoItem item) async {
    _log("Deleting item " + item.text);
    _items.removeAt(item.position);
    return _setLink();
  }

  /// Places [item] at the [position] index within the todo list.
  Future reorderItems(TodoItem item, int position) async {
    _log("Insert " + item.text + " at position " + position.toString());
    _items.removeAt(item.position);
    _items.insert(position, new TodoItem(text: item.text, position: position));
    return _setLink();
  }

  Future _setLink() async {
    // This implementation can lose data because the Link needs an interface to
    // reconcile conflicts.
    (await _link).set(["items"],
        JSON.encode(_items.map((TodoItem item) => item.text).toList()));
  }
}

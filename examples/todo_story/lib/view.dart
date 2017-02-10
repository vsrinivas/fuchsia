// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';
import 'dart:math';

import 'package:application.lib.app.dart/app.dart';
import 'package:application.services/service_provider.fidl.dart';
import 'package:apps.modular.services.story/link.fidl.dart';
import 'package:apps.modular.services.story/module.fidl.dart';
import 'package:apps.modular.services.story/story.fidl.dart';
import 'package:flutter/material.dart';
import 'package:flutter/widgets.dart';
import 'package:lib.fidl.dart/bindings.dart';

import "./data.dart";
import "./generator.dart";

/// Widget representing a list of [TodoItem]s.
class TodoListView extends StatefulWidget {
  TodoListView(LinkConnector this._linkConnector);

  final LinkConnector _linkConnector;

  @override
  TodoListViewState createState() => new TodoListViewState(_linkConnector);
}

class TodoListViewState extends State<TodoListView> {
  LinkConnector _linkConnector;
  final Generator _generator = new Generator();
  final List<TodoItem> _items = new List<TodoItem>();
  InputValue _newTodo = InputValue.empty;

  TodoListViewState(this._linkConnector) {
    _linkConnector.setCallback((List<TodoItem> items) {
      setState(() {
        _items.clear();
        _items.addAll(items);
      });
    });
  }

  bool get _isComposing => _newTodo.text.length > 0;

  /// Removes one item from the list and from the ledger.
  void _handleItemRemoved(TodoItem item) {
    _linkConnector.removeItem(item);
  }

  /// Creates one item and adds it to the ledger.
  void _handleTodoCreated(InputValue value) {
    _newTodo = InputValue.empty;
    _linkConnector.addItem(value.text);
  }

  void _handleInputChanged(InputValue value) {
    // TODO(etiennej): Store this value in a link.
    setState(() {
      _newTodo = value;
    });
  }

  void _handleAddRandomTodo() {
    String todoText = _generator.makeContent();
    setState(() {
      _newTodo = new InputValue(text: todoText);
    });
  }

  /// Creates the text input.
  Widget _buildTextComposer() {
    ThemeData themeData = Theme.of(context);
    return new Column(children: <Widget>[
      new Row(children: <Widget>[
        new Flexible(
            child: new Input(
                value: _newTodo,
                hintText: 'Enter TODO',
                onSubmitted: _handleTodoCreated,
                onChanged: _handleInputChanged)),
        new Container(
            margin: new EdgeInsets.symmetric(horizontal: 4.0),
            child: new IconButton(
                icon: new Icon(Icons.send),
                onPressed:
                    _isComposing ? () => _handleTodoCreated(_newTodo) : null,
                color: _isComposing
                    ? themeData.accentColor
                    : themeData.disabledColor))
      ]),
      new RaisedButton(
          onPressed: _handleAddRandomTodo, child: new Text("Add random TODO"))
    ]);
  }

  @override
  Widget build(BuildContext context) {
    return new Scaffold(
        appBar: new AppBar(title: new Text('Todo Example (link)')),
        body: new Container(
            child: new Column(children: <Widget>[
          new Flexible(
              child: new ListView(
                  shrinkWrap: true,
                  children: _items.map((TodoItem item) {
                    return new LongPressDraggable(
                        child: new TodoItemView(
                            item: item,
                            onItemDeleted: _handleItemRemoved,
                            onDrop: _linkConnector.reorderItems),
                        data: item,
                        feedback: new Card(
                            child: new SizedBox(
                                height: 32.0,
                                width: 512.0,
                                child: new TodoItemView(item: item))));
                  }).toList())),
          _buildTextComposer()
        ])));
  }
}

/// Callback invoked when asking for the deletion of a [TodoItem].
typedef void ItemDeletedCallback(TodoItem item);

/// Widget for a single [TodoItem] inside a list.
class TodoItemView extends StatelessWidget {
  TodoItemView({TodoItem item, this.onItemDeleted, this.onDrop})
      : _item = item,
        super(key: new ObjectKey(item));

  final TodoItem _item;
  final ItemDeletedCallback onItemDeleted;
  final Function onDrop;

  @override
  Widget build(BuildContext context) {
    return new DragTarget<TodoItem>(
        onAccept: _onAcceptDrop,
        builder: (BuildContext context, List<TodoItem> data,
            List<dynamic> rejectedData) {
          List<ListItem> itemWidgets = new List.from(
              data.map((TodoItem item) => _itemToWidget(context, item)));
          itemWidgets.add(_itemToWidget(context, _item));
          return new Column(children: itemWidgets);
        });
  }

  ListItem _itemToWidget(BuildContext context, TodoItem todoItem) {
    return new ListItem(
      title: new Text(todoItem.text),
      onTap: () {
        onItemDeleted(todoItem);
      },
    );
  }

  void _onAcceptDrop(TodoItem item) {
    this.onDrop(item, _item.position);
  }
}

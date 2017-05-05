// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter/material.dart';
import 'package:flutter/widgets.dart';

import "./data.dart";
import "./generator.dart";

/// Widget representing a list of [TodoItem]s.
class TodoListView extends StatefulWidget {
  TodoListView(this._linkConnector);

  final LinkConnector _linkConnector;

  @override
  TodoListViewState createState() => new TodoListViewState(_linkConnector);
}

class TodoListViewState extends State<TodoListView> {
  final LinkConnector _linkConnector;
  final Generator _generator = new Generator();
  final List<TodoItem> _items = new List<TodoItem>();
  final TextEditingController _newTodoController = new TextEditingController();

  TodoListViewState(this._linkConnector) {
    _linkConnector.setCallback((List<TodoItem> items) {
      setState(() {
        _items.clear();
        _items.addAll(items);
      });
    });
  }

  bool get _isComposing => _newTodoController.text.isNotEmpty;

  /// Removes one item from the list and from the ledger.
  void _handleItemRemoved(TodoItem item) {
    _linkConnector.removeItem(item);
  }

  /// Creates one item and adds it to the ledger.
  void _handleTodoCreated(String value) {
    _newTodoController.clear();
    _linkConnector.addItem(value);
  }

  void _handleGenerateRandomTodo() {
    _newTodoController.text = _generator.makeContent();
  }

  /// Creates the text field.
  Widget _buildTextComposer() {
    ThemeData themeData = Theme.of(context);
    return new Column(children: <Widget>[
      new Row(children: <Widget>[
        new Flexible(
            child: new TextField(
                controller: _newTodoController,
                decoration: const InputDecoration(
                  hintText: 'Enter TODO',
                ),
                onSubmitted: _handleTodoCreated)),
        new Container(
            margin: new EdgeInsets.symmetric(horizontal: 4.0),
            child: new AnimatedBuilder(
                animation: _newTodoController,
                builder: (BuildContext context, Widget child) {
                  return new IconButton(
                      icon: new Icon(Icons.send),
                      onPressed: _isComposing
                          ? () => _handleTodoCreated(_newTodoController.text)
                          : null,
                      color: _isComposing
                          ? themeData.accentColor
                          : themeData.disabledColor);
                }))
      ]),
      new RaisedButton(
          onPressed: _handleGenerateRandomTodo,
          child: new Text("Generate random TODO"))
    ]);
  }

  @override
  Widget build(BuildContext context) {
    return new Scaffold(
        appBar: new AppBar(title: new Text('Todo Example (story)')),
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
          List<ListTile> itemWidgets = new List.from(
              data.map((TodoItem item) => _itemToWidget(context, item)));
          itemWidgets.add(_itemToWidget(context, _item));
          return new Column(children: itemWidgets);
        });
  }

  ListTile _itemToWidget(BuildContext context, TodoItem todoItem) {
    return new ListTile(
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

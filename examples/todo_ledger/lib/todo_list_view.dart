// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:flutter/material.dart';
import 'package:flutter/widgets.dart';

import 'todo_module.dart';

class _TodoItem extends StatelessWidget {
  _TodoItem({Key key, this.content, this.onDone}) : super(key: key);

  final String content;
  final VoidCallback onDone;

  @override
  Widget build(BuildContext context) {
    final ThemeData themeData = Theme.of(context);

    List<Widget> rowChildren = <Widget>[
      new Expanded(child: new Text(content)),
      new SizedBox(
          width: 72.0,
          child: new IconButton(
              icon: new Icon(Icons.done),
              color: themeData.primaryColor,
              onPressed: onDone)),
    ];
    return new Padding(
        padding: const EdgeInsets.symmetric(vertical: 16.0),
        child: new Row(
            mainAxisAlignment: MainAxisAlignment.spaceBetween,
            children: rowChildren));
  }
}

class TodoListView extends StatefulWidget {
  TodoListView(this._module);

  final TodoModule _module;

  @override
  TodoListViewState createState() => new TodoListViewState(_module);
}

class TodoListViewState extends State<TodoListView> {
  final double _appBarHeight = 120.0;

  TodoListViewState(this._module) {
    // TODO(ppi): decouple the widget from the module. This should go through
    // some class managing the model.
    _module.watch((Map<List<int>, String> items) => setState(() {
          _items = items;
        }));
  }

  final TodoModule _module;

  Map<List<int>, String> _items = <List<int>, String>{};

  @override
  Widget build(BuildContext context) {
    List<Widget> listItems = <Widget>[];
    _items.forEach((key, value) {
      listItems.add(new _TodoItem(
          content: value,
          onDone: () {
            _module.removeItem(key);
          }));
    });

    return new Theme(
        data: new ThemeData(
          brightness: Brightness.light,
          primarySwatch: Colors.indigo,
          platform: Theme.of(context).platform,
        ),
        child: new Scaffold(
            body: new CustomScrollView(slivers: <Widget>[
          new SliverAppBar(
            expandedHeight: _appBarHeight,
            pinned: true,
            flexibleSpace: new FlexibleSpaceBar(
              title: new Text('Todo List (Ledger)'),
            ),
          ),
          new SliverToBoxAdapter(child: _buildNewItemInput(context)),
          new SliverList(delegate: new SliverChildListDelegate(listItems))
        ])));
  }

  Widget _buildNewItemInput(BuildContext context) {
    return new Row(children: <Widget>[
      new Expanded(
          child: new TextField(
              decoration: const InputDecoration(
                  hintText: 'What would you like to achieve today?'))),
      new SizedBox(
          width: 72.0,
          child: new IconButton(
              icon: new Icon(Icons.add),
              onPressed: () {
                this._module.addItem();
              })),
    ]);
  }
}

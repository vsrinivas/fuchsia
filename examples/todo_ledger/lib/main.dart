// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:application.lib.app.dart/app.dart';
import 'package:apps.modular.services.story/module.fidl.dart';
import 'package:flutter/material.dart';

import 'todo_list_view.dart';
import 'todo_module.dart';

final ApplicationContext _appContext = new ApplicationContext.fromStartupInfo();

void main() {
  final module = new TodoModule();

  _appContext.outgoingServices.addServiceForName(
    (request) {
      module.bind(request);
    },
    Module.serviceName,
  );

  runApp(new MaterialApp(
    title: 'Todo (Ledger)',
    home: new TodoListView(module),
    theme: new ThemeData(primarySwatch: Colors.blue),
  ));
}

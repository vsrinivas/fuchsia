// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:ui';

import 'package:flutter/material.dart';

/// A collection [AlertModel] instances.
class AlertsModel extends ChangeNotifier {
  final _alerts = <AlertModel>[];
  final ValueNotifier<AlertModel> _currentAlert =
      ValueNotifier<AlertModel>(null);

  List<AlertModel> get alerts => _alerts;
  AlertModel get currentAlert => _currentAlert.value;

  void addAlert(AlertModel alert) {
    _alerts.add(alert);
    _currentAlert.value = _alerts.last;
    notifyListeners();
  }

  void removeAlert(AlertModel alert) {
    _alerts.remove(alert);
    _currentAlert.value = _alerts.isNotEmpty ? _alerts.last : null;
    notifyListeners();
  }
}

/// A model that holds the content of an alert message.
class AlertModel {
  final String header;
  final String title;
  final String description;
  final _actions = <ActionModel>[];

  /// The model that holds a list of [AlertModel]s including this one.
  final AlertsModel alerts;

  List<ActionModel> get actions => _actions;

  AlertModel({
    @required this.alerts,
    @required this.title,
    this.header = '',
    this.description = '',
    List<ActionModel> actions = const <ActionModel>[],
  })  : assert(title.isNotEmpty),
        assert(alerts != null) {
    _actions.addAll(actions);
  }

  void close() {
    alerts.removeAlert(this);
  }
}

class ActionModel {
  final String name;
  final VoidCallback action;

  ActionModel(this.name, this.action);
}

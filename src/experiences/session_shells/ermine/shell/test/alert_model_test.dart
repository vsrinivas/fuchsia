// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// ignore: implementation_imports
import 'package:ermine/src/models/alert_model.dart';
import 'package:test/test.dart';

void main() {
  AlertsModel model;

  setUp(() {
    model = AlertsModel();
  });

  test(
      'AlertsModel should be able to add and remove AlertModels and notify '
      'its listeners when ever it does.', () {
    var isNotified = false;
    var isActionCalled = false;

    final alertA = AlertModel(
      header: 'component A',
      title: 'Title A',
      description: 'Hello',
      alerts: model,
      actions: [ActionModel('Action A', () => isActionCalled = true)],
    );
    final alertB = AlertModel(
      header: 'component B',
      title: 'Title B',
      alerts: model,
    );

    void onListened() {
      isNotified = true;
    }

    model.addListener(onListened);

    // The model should be empty and have never notified to its listeners.
    expect(model.alerts.length, 0);
    expect(isNotified, isFalse);

    // Adds an alert (alertA) to the model
    model.addAlert(alertA);

    // The model should have one alert and it should match to alertA.
    // The listener should be notified.
    expect(isNotified, isTrue);
    expect(model.alerts.length, 1);
    expect(model.currentAlert, alertA);
    expect(model.currentAlert.header, alertA.header);
    expect(model.currentAlert.description, alertA.description);
    expect(model.currentAlert.actions.length, 1);
    expect(model.currentAlert.actions.first.name, 'Action A');

    // The callback of the current alert in the model should be executed when
    // called.
    expect(isActionCalled, isFalse);
    model.currentAlert.actions[0].callback();
    expect(isActionCalled, isTrue);

    isNotified = false;
    expect(isNotified, isFalse);

    // Adds another alert (alertB) to the model
    model.addAlert(alertB);

    // The current alert should be updated to the newly added alert and it
    // should match to alertB.
    // The listener should be notified.
    expect(isNotified, isTrue);
    expect(model.alerts.length, 2);
    expect(model.currentAlert, alertB);
    expect(model.currentAlert.header, alertB.header);
    expect(model.currentAlert.description, isEmpty);
    expect(model.currentAlert.actions, isEmpty);

    isNotified = false;
    expect(isNotified, isFalse);

    // Removes alertB directly from the model.
    model.removeAlert(alertB);

    // alertB should be removed from the model and the listener should be
    // notified.
    expect(isNotified, isTrue);
    expect(model.alerts.length, 1);
    expect(model.currentAlert, alertA);

    isNotified = false;
    expect(isNotified, isFalse);

    // Removes alertA from the model by calling its close().
    alertA.close();

    // alertA should be gone from the model and there should be no remaining
    // alerts in the model.
    expect(isNotified, isTrue);
    expect(model.alerts, isEmpty);
    expect(model.currentAlert, isNull);

    model.removeListener(onListened);
  });

  test('AlertsModel should distinguish alerts when they have the same content.',
      () {
    // Creats two AlertModel with the same content.
    final alertA = AlertModel(
      header: 'header',
      title: 'title',
      description: 'description',
      alerts: model,
    );
    final alertB = AlertModel(
      header: 'header',
      title: 'title',
      description: 'description',
      alerts: model,
    );

    // The AlertModels should have different IDs.
    final idA = alertA.id;
    final idB = alertB.id;
    expect(idA, isNot(equals(idB)));

    expect(model.alerts.length, 0);

    // Add the AlertModels to the AlertsModel.
    model..addAlert(alertA)..addAlert(alertB);
    expect(model.alerts.length, 2);

    // The AlertsModel should remove the given AlertModel.
    model.removeAlert(alertA);
    expect(model.alerts.length, 1);
    expect(model.alerts[0].id, idB);
  });
}

// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';

import 'package:flutter/services.dart';
import 'package:modular/log.dart';
import 'package:mojo_services/notifications/notifications.mojom.dart';

import 'story.dart';

typedef void OnNotificationSelected(Story story);

/// Responsible for showing system notifications for shared stories. Clicking on
/// the notification opens the story.
class Notifier {
  final Logger _log = log('Notifier');
  NotificationServiceProxy _notificationService =
      new NotificationServiceProxy.unbound();

  Future<Null> postNotification(Story sharedStory,
      final OnNotificationSelected onNotificationSelected) async {
    if (onNotificationSelected == null) {
      _log.warning(
          "Notification cannot be shown as there is no callback passed.");
      return;
    }
    if (!_notificationService.ctrl.isBound) {
      // TODO(ksimbili): Decide when to close this.
      _notificationService = shell.connectToApplicationService(
          "mojo:notifications", NotificationService.connectToService);
    }
    final _StoryNotificationClient client =
        new _StoryNotificationClient(onNotificationSelected, sharedStory);
    _notificationService.post(
        new NotificationData()
          ..title = 'New Story'
          ..text = sharedStory.title
          ..playSound = true,
        client.stub,
        null);
  }
}

// Client listens for action on the notification. Selecting the notification
// opens the story.
class _StoryNotificationClient extends NotificationClient {
  final NotificationClientStub stub = new NotificationClientStub.unbound();
  final OnNotificationSelected _onNotificationSelected;
  final Story _notifiedStory;

  _StoryNotificationClient(this._onNotificationSelected, this._notifiedStory) {
    stub.impl = this;
  }

  @override
  Future<Null> onSelected() {
    _onNotificationSelected(_notifiedStory);
    return stub.close();
  }

  @override
  Future<Null> onDismissed() {
    return stub.close();
  }
}

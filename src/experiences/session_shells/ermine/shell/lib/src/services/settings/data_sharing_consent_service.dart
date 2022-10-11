// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:ermine/src/services/settings/task_service.dart';
import 'package:fidl_fuchsia_settings/fidl_async.dart';
import 'package:fuchsia_logger/logger.dart';
import 'package:fuchsia_services/services.dart';

typedef ConsentUpdateCallback = void Function(bool);

/// Defines a [TaskService] that toggles user's consent on Usage & Diagnostics data sharing.
class DataSharingConsentService implements TaskService {
  late PrivacyProxy _proxy;
  late final ConsentUpdateCallback onChanged;

  DataSharingConsentService();

  Future<bool> getCurrentConsent() async {
    try {
      final settings = await _proxy.watch();
      return settings.userDataSharingConsent ?? false;
    } on Exception catch (e) {
      log.warning('Failed to retrieve the current privacy status: $e');
    }
    return false;
  }

  void setConsent({required bool consent}) {
    final settings = PrivacySettings(userDataSharingConsent: consent);
    log.info('Setting up the privacy status to ${consent.toString()}');
    _proxy.set(settings);

    onChanged(consent);
  }

  @override
  Future<void> start() async {
    _proxy = PrivacyProxy();
    Incoming.fromSvcPath().connectToService(_proxy);

    final status = await getCurrentConsent();
    log.info('Starting the service with the current privacy status: $status');
    onChanged(status);
  }

  @override
  Future<void> stop() async {
    dispose();
  }

  @override
  void dispose() {
    _proxy.ctrl.close();
  }
}

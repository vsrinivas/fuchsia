// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:io';

import 'package:fidl_fuchsia_settings/fidl_async.dart';
import 'package:fuchsia_services/services.dart';

/// Defines a service to gather consent to send data usage.
class PrivacyConsentService {
  /// Returns the privacy policy content.
  late String privacyPolicy;

  final _proxy = PrivacyProxy();

  PrivacyConsentService() {
    Incoming.fromSvcPath().connectToService(_proxy);

    // TODO(fxbug.dev/73407): Replace with actual privacy policy.
    File file = File('/pkg/data/privacy_policy.txt');
    if (file.existsSync()) {
      privacyPolicy = file.readAsStringSync();
    } else {
      privacyPolicy = 'Error: Unable to load privacy policy.';
    }
  }

  void setConsent({required bool consent}) {
    final settings = PrivacySettings(userDataSharingConsent: consent);
    _proxy.set(settings);
  }

  void dispose() {
    _proxy.ctrl.close();
  }
}

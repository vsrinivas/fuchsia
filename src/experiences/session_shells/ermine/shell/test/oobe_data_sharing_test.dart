// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// ignore_for_file: implementation_imports
import 'package:ermine/src/widgets/oobe/data_sharing.dart';
import 'package:fidl/fidl.dart';
import 'package:fidl_fuchsia_settings/fidl_async.dart';
import 'package:mockito/mockito.dart';
import 'package:test/test.dart';

void main() {
  MockPrivacyProxy privacySettingsProxy;
  MockPrivacyProxyController privacySettingsProxyController;
  void onBack() {}
  void onNext() {}
  DataSharing dataSharing;

  setUp(() async {
    privacySettingsProxy = MockPrivacyProxy();
    privacySettingsProxyController = MockPrivacyProxyController();
    when(privacySettingsProxy.ctrl).thenReturn(privacySettingsProxyController);

    PrivacyModel model =
        PrivacyModel(privacySettingsService: privacySettingsProxy);
    dataSharing = DataSharing(onBack: onBack, onNext: onNext, model: model);
  });

  tearDown(() async {
    privacySettingsProxyController.close();
  });

  test('DataSharing should set user consent value to true when user agrees.',
      () async {
    dataSharing.agree();
    verify(privacySettingsProxy
            .set(PrivacySettings(userDataSharingConsent: true)))
        .called(1);
  });

  test(
      'DataSharing should set user consent value to false when user disagrees.',
      () async {
    dataSharing.disagree();
    verify(privacySettingsProxy
            .set(PrivacySettings(userDataSharingConsent: false)))
        .called(1);
  });
}

// Mock classes.
class MockPrivacyProxy extends Mock implements PrivacyProxy {}

class MockPrivacyProxyController extends Mock
    implements AsyncProxyController<Privacy> {}

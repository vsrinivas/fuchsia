// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:ermine_ui/ermine_ui.dart';
import 'package:fidl_fuchsia_settings/fidl_async.dart';
import 'package:flutter/material.dart';
import 'package:fuchsia_services/services.dart' show Incoming;
import 'package:internationalization/strings.dart';

import '../../utils/styles.dart';
import 'oobe_buttons.dart';
import 'oobe_header.dart';

const double kPrivacyPolicyWidth = 856;
const double kPrivacyPolicyVerticalMargin = 46;

// TODO(fxbug.dev/73407): Replace with actual privacy policy.
const String kPlaceholderPrivacyPolicy =
    '''We build a range of services that help millions of people daily to explore and interact with the world in new ways. Our services include:

Google apps, sites, and devices, like Search, YouTube, and Google Home
Platforms like the Chrome browser and Android operating system
Products that are integrated into third-party apps and sites, like ads and embedded Google Maps

You can use our services in a variety of ways to manage your privacy. For example, you can sign up for a Google Account if you want to create and manage content like emails and photos, or see more relevant search results. And you can use many Google services when you’re signed out or without creating an account at all, like searching on Google or watching YouTube videos. You can also choose to browse the web privately using Chrome in Incognito mode. And across our services, you can adjust your privacy settings to control what we collect and how your information is used.
To help explain things as clearly as possible, we’ve added examples, explanatory videos, and definitions for key terms. And if you have any questions about this Privacy Policy, you can contact us.
''';

class DataSharing extends StatelessWidget {
  final VoidCallback onBack;
  final VoidCallback onNext;
  final PrivacyModel model;

  const DataSharing(
      {@required this.onBack, @required this.onNext, @required this.model});

  factory DataSharing.withSvcPath(VoidCallback onBack, VoidCallback onNext) {
    final privacySettingsService = PrivacyProxy();
    Incoming.fromSvcPath().connectToService(privacySettingsService);
    final privacyModel =
        PrivacyModel(privacySettingsService: privacySettingsService);
    return DataSharing(onBack: onBack, onNext: onNext, model: privacyModel);
  }

  @override
  Widget build(BuildContext context) {
    return AnimatedBuilder(
      animation: model.privacyPolicyVisibility,
      builder: (context, _) =>
          model.privacyPolicyVisibility.value ? _buildPolicy() : _buildMain(),
    );
  }

  Widget _buildMain() => Column(
        children: <Widget>[
          OobeHeader(
            Strings.dataSharingTitle,
            [
              DescriptionModel(text: Strings.dataSharingDesc1),
              DescriptionModel(
                text: Strings.dataSharingDesc2,
                onClicked: model.showPolicy,
                style: ErmineTextStyles.headline4
                    .copyWith(decoration: TextDecoration.underline),
              ),
              DescriptionModel(text: Strings.dataSharingDesc3),
            ],
          ),
          Expanded(
            child: Container(
              margin: EdgeInsets.symmetric(
                  vertical: ErmineStyle.kOobeBodyVerticalMargins),
            ),
          ),
          OobeButtons([
            OobeButtonModel(Strings.back, onBack),
            OobeButtonModel(Strings.disagree, disagree),
            OobeButtonModel(Strings.agree, agree, filled: true),
          ]),
        ],
      );

  Widget _buildPolicy() => Column(
        children: <Widget>[
          OobeHeader(Strings.privacyPolicyTitle, <DescriptionModel>[]),
          Expanded(
            child: Container(
              margin:
                  EdgeInsets.symmetric(vertical: kPrivacyPolicyVerticalMargin),
              width: kPrivacyPolicyWidth,
              child: SingleChildScrollView(
                scrollDirection: Axis.vertical,
                child: Text(
                  // TODO(fxbug.dev/73407): Replace with actual privacy policy.
                  kPlaceholderPrivacyPolicy,
                  style: ErmineTextStyles.headline4,
                ),
              ),
            ),
          ),
          OobeButtons([
            OobeButtonModel(Strings.close, model.hidePolicy),
          ]),
        ],
      );

  void agree() {
    model.setConsent(value: true);
    onNext();
  }

  void disagree() {
    model.setConsent(value: false);
    onNext();
  }
}

class PrivacyModel {
  PrivacyProxy privacySettingsService;
  ValueNotifier<bool> privacyPolicyVisibility = ValueNotifier(false);

  PrivacyModel({this.privacySettingsService});

  void dispose() {
    privacySettingsService.ctrl.close();
    privacySettingsService = null;
  }

  void setConsent({bool value}) {
    final newPrivacySettings = PrivacySettings(userDataSharingConsent: value);
    privacySettingsService.set(newPrivacySettings);
  }

  void showPolicy() {
    privacyPolicyVisibility.value = true;
  }

  void hidePolicy() {
    privacyPolicyVisibility.value = false;
  }
}

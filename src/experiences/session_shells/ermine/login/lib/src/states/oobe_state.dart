// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:ui';

import 'package:ermine_utils/ermine_utils.dart';
import 'package:fuchsia_scenic_flutter/fuchsia_view.dart';
import 'package:login/src/services/auth_service.dart';
import 'package:login/src/services/channel_service.dart';
import 'package:login/src/services/automator_service.dart';
import 'package:login/src/services/device_service.dart';
import 'package:login/src/services/privacy_consent_service.dart';
import 'package:login/src/services/shell_service.dart';
import 'package:login/src/services/ssh_keys_service.dart';
import 'package:login/src/states/oobe_state_impl.dart';

/// The oobe screens the user navigates through.
enum OobeScreen { /* channel, sshKeys,*/ loading, dataSharing, password, done }

/// The screens the user navigates through to add SSH keys.
enum SshScreen { add, confirm, error, exit }

/// The ssh key import methods.
enum SshImport { github, manual }

const kContentWidth = 696.0;

/// Defines the state of an application view.
abstract class OobeState {
  Locale? get locale;
  OobeScreen get screen;
  bool get updateChannelsAvailable;
  String get currentChannel;
  List<String> get channels;
  Map<String, String> get channelDescriptions;
  SshScreen get sshScreen;
  String get sshKeyTitle;
  String get sshKeyDescription;
  SshImport get importMethod;
  List<String> get sshKeys;
  abstract int sshKeyIndex;
  bool get launchOobe;
  bool get ready;
  bool get hasAccount;
  bool get loginDone;
  bool get wait;
  bool get showDataSharing;
  String get authError;
  List<DialogInfo> get dialogs;

  FuchsiaViewConnection get ermineViewConnection;

  void showDialog(DialogInfo dialog);
  void setCurrentChannel(String channel);
  void nextScreen();
  void prevScreen();
  void setPrivacyConsent({required bool consent});
  void sshImportMethod(SshImport? method);
  void sshBackScreen();
  void sshAdd(String userNameOrKey);
  void setPassword(String password);
  void login(String password);
  void skip();
  void finish();
  void shutdown();
  void factoryReset();
  void resetAuthError();

  factory OobeState.fromEnv() {
    return OobeStateImpl(
      authService: AuthService(),
      deviceService: DeviceService(),
      automatorService: AutomatorService(),
      shellService: ShellService(),
      channelService: ChannelService(),
      sshKeysService: SshKeysService(),
      privacyConsentService: PrivacyConsentService(),
    );
  }

  void dispose();
}

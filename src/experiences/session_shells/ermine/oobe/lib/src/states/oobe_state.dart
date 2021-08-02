// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:ui';

import 'package:mobx/mobx.dart';
import 'package:oobe/src/services/channel_service.dart';
import 'package:oobe/src/services/privacy_consent_service.dart';
import 'package:oobe/src/services/ssh_keys_service.dart';
import 'package:oobe/src/states/oobe_state_impl.dart';

/// The oobe screens the user navigates through.
enum OobeScreen { channel, dataSharing, sshKeys, done }

/// The screens the user navigates through to add SSH keys.
enum SshScreen { add, confirm, error, exit }

/// The ssh key import methods.
enum SshImport { github, manual }

/// Defines the state of an application view.
abstract class OobeState with Store {
  ObservableStream<Locale> get localeStream;
  ObservableValue<OobeScreen> get screen;
  ObservableValue<bool> get updateChannelsAvailable;
  ObservableValue<String> get currentChannel;
  ObservableList<String> get channels;
  Map<String, String> get channelDescriptions;
  ObservableValue<SshScreen> get sshScreen;
  ObservableValue<String> get sshKeyTitle;
  ObservableValue<String> get sshKeyDescription;
  ObservableValue<SshImport> get importMethod;
  ObservableFuture<List<String>> get sshKeys;
  Observable<int> get sshKeyIndex;
  ObservableValue<bool> get privacyVisible;
  String get privacyPolicy;

  Action get setCurrentChannel;
  Action get nextScreen;
  Action get prevScreen;
  Action get agree;
  Action get disagree;
  Action get showPrivacy;
  Action get hidePrivacy;
  Action get sshImportMethod;
  Action get sshBackScreen;
  Action get sshAdd;
  Action get skip;
  Action get finish;

  factory OobeState.fromEnv() {
    return OobeStateImpl(
      channelService: ChannelService(),
      sshKeysService: SshKeysService(),
      privacyConsentService: PrivacyConsentService(),
    );
  }

  void dispose();
}

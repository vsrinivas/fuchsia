// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:isolate';
import 'dart:ui';

import 'package:internationalization/strings.dart';
import 'package:mobx/mobx.dart';
import 'package:oobe/src/services/channel_service.dart';
import 'package:oobe/src/services/privacy_consent_service.dart';
import 'package:oobe/src/services/ssh_keys_service.dart';
import 'package:oobe/src/states/oobe_state.dart';
import 'package:oobe/src/utils/mobx_disposable.dart';
import 'package:oobe/src/utils/mobx_extensions.dart';

/// Defines an implementation of [ViewState].
class OobeStateImpl with Disposable implements OobeState {
  final ChannelService channelService;
  final SshKeysService sshKeysService;
  final PrivacyConsentService privacyConsentService;

  OobeStateImpl({
    required this.channelService,
    required this.sshKeysService,
    required this.privacyConsentService,
  }) : localeStream = channelService.stream.asObservable() {
    privacyPolicy = privacyConsentService.privacyPolicy;

    channelService.onConnected = (connected) => runInAction(() async {
          if (connected) {
            channels
              ..clear()
              ..addAll(await channelService.channels);
            currentChannel.value = await channelService.currentChannel;
          }
          updateChannelsAvailable.value = connected;
        });
  }

  @override
  void dispose() {
    super.dispose();
    channelService.dispose();
    privacyConsentService.dispose();
    sshKeysService.dispose();
  }

  @override
  final ObservableStream<Locale> localeStream;

  @override
  final Observable<OobeScreen> screen = OobeScreen.channel.asObservable();

  @override
  final Observable<bool> updateChannelsAvailable = false.asObservable();

  @override
  final ObservableList<String> channels = ObservableList.of([]);

  @override
  final Observable<String> currentChannel = ''.asObservable();

  @override
  final channelDescriptions = ChannelService.descriptions;

  @override
  final Observable<SshScreen> sshScreen = SshScreen.add.asObservable();

  @override
  late final sshKeyTitle = (() {
    switch (sshScreen.value) {
      case SshScreen.add:
        return Strings.oobeSshKeysAddTitle;
      case SshScreen.confirm:
        return Strings.oobeSshKeysConfirmTitle;
      case SshScreen.error:
        return Strings.oobeSshKeysErrorTitle;
      case SshScreen.exit:
        return Strings.oobeSshKeysSuccessTitle;
    }
  }).asComputed();

  @override
  late final sshKeyDescription = (() {
    switch (sshScreen.value) {
      case SshScreen.add:
        return Strings.oobeSshKeysAddDesc;
      case SshScreen.confirm:
        return Strings.oobeSshKeysSelectionDesc(sshKeys.value?.length ?? 0);
      case SshScreen.error:
        return errorMessage;
      case SshScreen.exit:
        return Strings.oobeSshKeysSuccessDesc;
    }
  }).asComputed();

  @override
  final Observable<SshImport> importMethod = SshImport.github.asObservable();

  @override
  ObservableFuture<List<String>> sshKeys =
      Future<List<String>>.value([]).asObservable();

  @override
  final sshKeyIndex = 0.asObservable();

  @override
  final Observable<bool> privacyVisible = false.asObservable();

  @override
  late final String privacyPolicy;

  @override
  late final Action setCurrentChannel = (channel) async {
    await channelService.setCurrentChannel(channel);
    currentChannel.value = await channelService.currentChannel;
  }.asAction();

  @override
  late final Action prevScreen = () {
    if (screen.value.index > 0) {
      // TODO(fxbug.dev/73407): Skip data sharing screen until privacy policy is
      // finalized.
      if (screen.value == OobeScreen.sshKeys) {
        screen.value = OobeScreen.values[screen.value.index - 2];
      } else {
        screen.value = OobeScreen.values[screen.value.index - 1];
      }
    }
  }.asAction();

  @override
  late final Action nextScreen = () {
    if (screen.value.index + 1 < OobeScreen.done.index) {
      // TODO(fxbug.dev/73407): Skip data sharing screen until privacy policy is
      // finalized.
      if (screen.value == OobeScreen.channel) {
        screen.value = OobeScreen.values[screen.value.index + 2];
      } else {
        screen.value = OobeScreen.values[screen.value.index + 1];
      }
    }
  }.asAction();

  @override
  late final Action agree = () {
    privacyConsentService.setConsent(consent: true);
    nextScreen();
  }.asAction();

  @override
  late final Action disagree = () {
    privacyConsentService.setConsent(consent: false);
    nextScreen();
  }.asAction();

  @override
  late final Action showPrivacy = () {
    privacyVisible.value = true;
  }.asAction();

  @override
  late final Action hidePrivacy = () {
    privacyVisible.value = false;
  }.asAction();

  @override
  late final Action sshImportMethod = (method) {
    importMethod.value = method;
  }.asAction();

  @override
  late final Action sshBackScreen = () {
    if (sshScreen.value == SshScreen.add) {
      prevScreen();
    } else {
      sshScreen.value = SshScreen.add;
    }
  }.asAction();

  String errorMessage = '';
  @override
  late final Action sshAdd = (userNameOrKey) {
    if (sshScreen.value == SshScreen.add) {
      if (importMethod.value == SshImport.github) {
        final future = sshKeysService.fetchKeys(userNameOrKey);
        sshKeys = future.asObservable();
        future.then((keys) {
          if (keys.isEmpty) {
            errorMessage = Strings.oobeSshKeysGithubErrorDesc(userNameOrKey);
            sshScreen.value = SshScreen.error;
          } else {
            sshScreen.value = SshScreen.confirm;
          }
        }).catchError((e) {
          errorMessage = e.message;
          sshScreen.value = SshScreen.error;
        });
      } else {
        // Add it manually.
        _saveKey(userNameOrKey);
      }
    } else if (sshScreen.value == SshScreen.confirm) {
      if (sshKeys.value?.isNotEmpty == true) {
        final selectedKey = sshKeys.value!.elementAt(sshKeyIndex.value);
        _saveKey(selectedKey);
      }
    }
  }.asAction();

  void _saveKey(String key) {
    sshKeysService
        .addKey(key)
        .then((_) => runInAction(() {
              sshScreen.value = SshScreen.exit;
            }))
        .catchError((e) {
      errorMessage = Strings.oobeSshKeysFidlErrorDesc;
      sshScreen.value = SshScreen.error;
    });
  }

  @override
  late final Action skip = () {
    sshScreen.value = SshScreen.exit;
  }.asAction();

  @override
  late final Action finish = () {
    dispose();
    Isolate.current.kill();
  }.asAction();
}

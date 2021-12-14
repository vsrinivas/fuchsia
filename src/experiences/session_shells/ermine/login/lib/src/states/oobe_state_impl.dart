// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:convert' show json;
import 'dart:io';
import 'dart:ui';

import 'package:ermine_utils/ermine_utils.dart';
import 'package:flutter/services.dart';
import 'package:fuchsia_logger/logger.dart';
import 'package:fuchsia_scenic_flutter/fuchsia_view.dart';
import 'package:fuchsia_services/services.dart';
import 'package:internationalization/strings.dart';
import 'package:mobx/mobx.dart';
import 'package:login/src/services/auth_service.dart';
import 'package:login/src/services/channel_service.dart';
import 'package:login/src/services/device_service.dart';
import 'package:login/src/services/privacy_consent_service.dart';
import 'package:login/src/services/shell_service.dart';
import 'package:login/src/services/ssh_keys_service.dart';
import 'package:login/src/states/oobe_state.dart';

/// Defines an implementation of [OobeState].
class OobeStateImpl with Disposable implements OobeState {
  static const kDefaultConfigJson = '/config/data/ermine/startup_config.json';
  static const kStartupConfigJson = '/data/startup_config.json';

  final ComponentContext componentContext;
  final AuthService authService;
  final ChannelService channelService;
  final DeviceService deviceService;
  final SshKeysService sshKeysService;
  final ShellService shellService;
  final PrivacyConsentService privacyConsentService;

  OobeStateImpl({
    required this.authService,
    required this.deviceService,
    required this.shellService,
    required this.channelService,
    required this.sshKeysService,
    required this.privacyConsentService,
  })  : componentContext = ComponentContext.create(),
        _localeStream = channelService.stream.asObservable() {
    authService.outgoing = componentContext.outgoing;
    privacyPolicy = privacyConsentService.privacyPolicy;
    shellService.onShellExit = _onErmineShellExit;

    componentContext.outgoing.serveFromStartupInfo();

    channelService.onConnected = (connected) => runInAction(() async {
          if (connected) {
            channels
              ..clear()
              ..addAll(await channelService.channels);
            _currentChannel.value = await channelService.currentChannel;
          }
          _updateChannelsAvailable.value = connected;
        });

    // We cannot load MaterialIcons font file from pubspec.yaml. So load it
    // explicitly.
    File file = File('/pkg/data/MaterialIcons-Regular.otf');
    if (file.existsSync()) {
      FontLoader('MaterialIcons')
        ..addFont(() async {
          final bytes = await file.readAsBytes();
          return bytes.buffer.asByteData();
        }())
        ..load();
    }
  }

  @override
  void dispose() {
    super.dispose();
    authService.dispose();
    channelService.dispose();
    privacyConsentService.dispose();
    sshKeysService.dispose();
    shellService.dispose();
  }

  @override
  bool get launchOobe => _launchOobe.value;
  set launchOobe(bool value) => runInAction(() => _launchOobe.value = value);
  late final Observable<bool> _launchOobe = Observable<bool>(() {
    // This should be called only after startup services are ready.
    assert(ready, 'Startup services are not initialized.');

    // Skip OOBE if an account already exists on the device.
    if (authService.hasAccount) {
      return false;
    }

    File config = File(kStartupConfigJson);
    // If startup config does not exist, open the default config.
    if (!config.existsSync()) {
      config = File(kDefaultConfigJson);
    }
    // If default config is missing, log error and return defaults.
    if (!config.existsSync()) {
      log.severe('Missing startup and default configs. Skipping OOBE.');
      return false;
    }
    final data = json.decode(config.readAsStringSync()) ?? {};
    return data['launch_oobe'] == true;
  }());

  @override
  bool get ready => _ready.value;
  late final _ready = (() {
    return shellService.ready && authService.ready;
  }).asComputed();

  @override
  bool get loginDone => _loginDone.value;
  // TODO(http://fxb/85576): Initialize to false when password authentication is enabled.
  final _loginDone = true.asObservable();

  @override
  Locale? get locale => _localeStream.value;
  final ObservableStream<Locale> _localeStream;

  FuchsiaViewConnection? _ermineViewConnection;
  @override
  FuchsiaViewConnection get ermineViewConnection =>
      _ermineViewConnection ??= shellService.launchErmineShell();

  @override
  OobeScreen get screen => _screen.value;
  final Observable<OobeScreen> _screen = OobeScreen.password.asObservable();

  @override
  bool get updateChannelsAvailable => _updateChannelsAvailable.value;
  final Observable<bool> _updateChannelsAvailable = false.asObservable();

  @override
  final ObservableList<String> channels = ObservableList.of([]);

  @override
  String get currentChannel => _currentChannel.value;
  final Observable<String> _currentChannel = ''.asObservable();

  @override
  final channelDescriptions = ChannelService.descriptions;

  @override
  SshScreen get sshScreen => _sshScreen.value;
  final Observable<SshScreen> _sshScreen = SshScreen.add.asObservable();

  @override
  String get sshKeyTitle => _sshKeyTitle.value;
  late final _sshKeyTitle = (() {
    switch (sshScreen) {
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
  String get sshKeyDescription => _sshKeyDescription.value;
  late final _sshKeyDescription = (() {
    switch (sshScreen) {
      case SshScreen.add:
        return Strings.oobeSshKeysAddDesc;
      case SshScreen.confirm:
        return Strings.oobeSshKeysSelectionDesc(sshKeys.length);
      case SshScreen.error:
        return errorMessage;
      case SshScreen.exit:
        return Strings.oobeSshKeysSuccessDesc;
    }
  }).asComputed();

  @override
  SshImport get importMethod => _importMethod.value;
  final Observable<SshImport> _importMethod = SshImport.github.asObservable();

  @override
  final List<String> sshKeys = ObservableList<String>();

  @override
  int get sshKeyIndex => _sshKeyIndex.value;
  @override
  set sshKeyIndex(int value) => runInAction(() => _sshKeyIndex.value = value);
  final _sshKeyIndex = 0.asObservable();

  @override
  bool get privacyVisible => _privacyVisible.value;
  final Observable<bool> _privacyVisible = false.asObservable();

  @override
  late final String privacyPolicy;

  @override
  String get authError => _authError.value;
  final _authError = ''.asObservable();

  @override
  bool get wait => _wait.value;
  final _wait = false.asObservable();

  @override
  void setCurrentChannel(String channel) => runInAction(() async {
        await channelService.setCurrentChannel(channel);
        _currentChannel.value = await channelService.currentChannel;
      });

  @override
  void prevScreen() => runInAction(() {
        if (screen.index > 0) {
          _screen.value = OobeScreen.values[screen.index - 1];
        }
      });

  @override
  void nextScreen() => runInAction(() {
        if (screen.index + 1 <= OobeScreen.done.index) {
          _screen.value = OobeScreen.values[screen.index + 1];
        }
      });

  @override
  void agree() => runInAction(() {
        privacyConsentService.setConsent(consent: true);
        nextScreen();
      });

  @override
  void disagree() => runInAction(() {
        privacyConsentService.setConsent(consent: false);
        nextScreen();
      });

  @override
  void showPrivacy() => runInAction(() => _privacyVisible.value = true);

  @override
  void hidePrivacy() => runInAction(() => _privacyVisible.value = false);

  @override
  void sshImportMethod(SshImport? method) =>
      runInAction(() => _importMethod.value = method!);

  @override
  void sshBackScreen() => runInAction(() {
        if (sshScreen == SshScreen.add) {
          prevScreen();
        } else {
          _sshScreen.value = SshScreen.add;
        }
      });

  String errorMessage = '';
  @override
  void sshAdd(String userNameOrKey) => runInAction(() {
        if (sshScreen == SshScreen.add) {
          if (importMethod == SshImport.github) {
            sshKeysService.fetchKeys(userNameOrKey).then((keys) {
              if (keys.isEmpty) {
                errorMessage =
                    Strings.oobeSshKeysGithubErrorDesc(userNameOrKey);
                _sshScreen.value = SshScreen.error;
              } else {
                sshKeys
                  ..clear()
                  ..addAll(keys);
                _sshScreen.value = SshScreen.confirm;
              }
            }).catchError((e) {
              errorMessage = e.message;
              _sshScreen.value = SshScreen.error;
            });
          } else {
            // Add it manually.
            _saveKey(userNameOrKey);
          }
        } else if (sshScreen == SshScreen.confirm) {
          if (sshKeys.isNotEmpty == true) {
            final selectedKey = sshKeys.elementAt(sshKeyIndex);
            _saveKey(selectedKey);
          }
        }
      });

  void _saveKey(String key) {
    sshKeysService
        .addKey(key)
        .then((_) => runInAction(() {
              _sshScreen.value = SshScreen.exit;
            }))
        .catchError((e) {
      errorMessage = Strings.oobeSshKeysFidlErrorDesc;
      _sshScreen.value = SshScreen.error;
    });
  }

  @override
  void skip() => runInAction(() => _sshScreen.value = SshScreen.exit);

  @override
  void finish() => runInAction(() {
        // Dismiss OOBE UX.
        launchOobe = false;

        // Mark login step as done.
        _loginDone.value = true;

        // Persistently record OOBE done.
        File(kStartupConfigJson).writeAsStringSync('{"launch_oobe":false}');
      });

  @override
  void setPassword(String password) async {
    try {
      _authError.value = '';
      _wait.value = true;
      await authService.createAccountWithPassword(password);
      _wait.value = false;
      nextScreen();
      // ignore: avoid_catches_without_on_clauses
    } catch (e) {
      log.shout('Caught exception during account creation: $e');
      _authError.value = e.toString();
    }
  }

  @override
  void login(String password) async {
    try {
      _authError.value = '';
      _wait.value = true;
      await authService.loginWithPassword(password);
      _wait.value = false;
      finish();
      // ignore: avoid_catches_without_on_clauses
    } catch (e) {
      log.shout('Caught exception during login: $e');
      _authError.value = e.toString();
    }
  }

  @override
  void shutdown() => deviceService.shutdown();

  void _onErmineShellExit() {
    runInAction(() {
      authService.logout().catchError((e) {
        log.shout('Caught exception during logout: $e');
      });
      shellService.disposeErmineShell();

      _ermineViewConnection = null;
      _loginDone.value = false;
    });
  }
}

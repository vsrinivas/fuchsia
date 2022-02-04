// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:convert' show json;
import 'dart:io';
import 'dart:ui';

import 'package:ermine_utils/ermine_utils.dart';
import 'package:flutter/services.dart';
import 'package:fuchsia_inspect/inspect.dart';
import 'package:fuchsia_logger/logger.dart';
import 'package:fuchsia_scenic_flutter/fuchsia_view.dart';
import 'package:fuchsia_services/services.dart';
import 'package:fuchsia_vfs/vfs.dart';
import 'package:internationalization/strings.dart';
import 'package:login/src/services/auth_service.dart';
import 'package:login/src/services/channel_service.dart';
import 'package:login/src/services/device_service.dart';
import 'package:login/src/services/privacy_consent_service.dart';
import 'package:login/src/services/shell_service.dart';
import 'package:login/src/services/ssh_keys_service.dart';
import 'package:login/src/states/oobe_state.dart';
import 'package:mobx/mobx.dart';

const kHostedDirectories = 'hosted_directories';

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
    privacyPolicy = privacyConsentService.privacyPolicy;
    shellService
      ..onShellReady = _onErmineShellReady
      ..onShellExit = _onErmineShellExit;
    deviceService
      ..onInspect = _onInspect
      ..serve(componentContext);

    // Create a directory that will host the account_data directory. This is needed
    // because the root directories are expected to exist at the time of serving.
    final hostedDirectories = PseudoDir();
    componentContext.outgoing
        .rootDir()
        .addNode(kHostedDirectories, hostedDirectories);

    authService.hostedDirectories = hostedDirectories;
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
  late final Observable<bool> _launchOobe = Observable<bool>(() {
    // This should be called only after startup services are ready.
    assert(ready, 'Startup services are not initialized.');

    // Check if the build allows OOBE.
    final startOOBE = () {
      File config = File(kDefaultConfigJson);
      // If default config is missing, log error and return defaults.
      if (!config.existsSync()) {
        log.severe('Missing startup and default configs. Skipping OOBE.');
        return false;
      }
      final data = json.decode(config.readAsStringSync()) ?? {};
      return data['launch_oobe'] == true;
    }();

    // TODO(http://fxb/85576): Remove once login and OOBE are mandatory.
    // If we are skipping OOBE, authenticate using empty password.
    if (!startOOBE) {
      _performNullLogin().then((_) {
        runInAction(() => _loginDone.value = true);
      });
    }

    return startOOBE;
  }());

  @override
  bool get ready => _ready.value;
  late final _ready = (() {
    return shellService.ready && authService.ready;
  }).asComputed();

  @override
  bool get hasAccount {
    // This should be called only after startup services are ready.
    assert(ready, 'Startup services are not initialized.');
    return authService.hasAccount;
  }

  @override
  bool get loginDone => _loginDone.value;
  final _loginDone = false.asObservable();

  @override
  Locale? get locale => _localeStream.value;
  final ObservableStream<Locale> _localeStream;

  final _ermineViewConnection = Observable<FuchsiaViewConnection?>(null);
  @override
  FuchsiaViewConnection get ermineViewConnection =>
      _ermineViewConnection.value ??= shellService.launchErmineShell();

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
        // Mark login step as done.
        _loginDone.value = true;
      });

  @override
  void setPassword(String password) async {
    try {
      runInAction(() {
        _authError.value = '';
        _wait.value = true;
      });
      await authService.createAccountWithPassword(password);
      runInAction(() => _wait.value = false);
      nextScreen();
      // ignore: avoid_catches_without_on_clauses
    } catch (e) {
      log.shout('Caught exception during account creation: $e');
      runInAction(() {
        _wait.value = false;
        _authError.value = authService.errorFromException(e);
      });
    }
  }

  @override
  void login(String password) async {
    try {
      runInAction(() {
        _authError.value = '';
        _wait.value = true;
      });
      await authService.loginWithPassword(password);
      runInAction(() => _wait.value = false);
      finish();
      // ignore: avoid_catches_without_on_clauses
    } catch (e) {
      log.shout('Caught exception during login: $e');
      runInAction(() {
        _wait.value = false;
        _authError.value = authService.errorFromException(e);
      });
    }
  }

  @override
  void shutdown() => deviceService.shutdown();

  @override
  void factoryReset() => authService.factoryReset();

  bool _ermineReady = false;
  void _onErmineShellReady() {
    _ermineReady = true;
  }

  void _onErmineShellExit() {
    _ermineReady = false;

    runInAction(() => _loginDone.value = false);

    // Define a local method to run after logout below.
    void postLogout() {
      // If OOBE is disabled, perform empty password re-login.
      if (!launchOobe) {
        _performNullLogin().then((_) {
          runInAction(() {
            shellService.disposeErmineShell();
            _ermineViewConnection.value = null;
            _loginDone.value = true;
          });
        });
      } else {
        // Display login screen again.
        runInAction(() {
          shellService.disposeErmineShell();
          _ermineViewConnection.value = null;
        });
      }
    }

    // Logout and call [postLogout] on both success and error case.
    authService.logout().then((_) => postLogout()).catchError((e) {
      log.shout('Caught exception during logout: $e');
      postLogout();
    });
  }

  // TODO(http://fxb/85576): Remove once login and OOBE are mandatory.
  // If we are skipping OOBE, authenticate using empty password.
  Future<void> _performNullLogin() async {
    log.info('Skipped OOBE, authenticating with empty password.');
    try {
      authService.hasAccount
          ? await authService.loginWithPassword('')
          : await authService.createAccountWithPassword('');
      // ignore: avoid_catches_without_on_clauses
    } catch (e) {
      log.shout('Account found: ${authService.hasAccount}.'
          ' Caught exception during authentication: $e');
    }
  }

  void _onInspect(Node node) {
    node.boolProperty('ready')!.setValue(ready);
    node.boolProperty('launchOOBE')!.setValue(launchOobe);
    node.boolProperty('ermineReady')!.setValue(_ermineReady);
    node.boolProperty('authenticated')!.setValue(loginDone);
    if (hasAccount) {
      node.stringProperty('screen')!.setValue('login');
    } else {
      node.stringProperty('screen')!.setValue(screen.name);
    }
  }
}

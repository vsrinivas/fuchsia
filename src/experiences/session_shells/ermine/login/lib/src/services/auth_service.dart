// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:io' as io;
import 'package:fidl/fidl.dart';
//ignore: unused_import
import 'package:ermine_utils/ermine_utils.dart';
import 'package:fidl_fuchsia_identity_account/fidl_async.dart';
import 'package:fidl_fuchsia_io/fidl_async.dart';
import 'package:fidl_fuchsia_recovery/fidl_async.dart';
import 'package:fuchsia_logger/logger.dart';
import 'package:fuchsia_services/services.dart';
import 'package:fuchsia_vfs/vfs.dart';
import 'package:internationalization/strings.dart';
import 'package:mobx/mobx.dart';
import 'package:zircon/zircon.dart';

const kDeprecatedAccountName = 'created_by_user';
const kSystemPickedAccountName = 'picked_by_system';
const kUserPickedAccountName = 'picked_by_user';
const kAccountDataDirectory = 'account_data';
const kAccountCacheDirectory = 'account_cache';
const kAccountTmpDirectory = 'account_tmp';
const kCacheSubdirectory = 'cache/';
const kIncomingTmpDirectory = '/tmp/';
const kTmpSubdirectory = 'account/';

enum AuthMode { automatic, manual }

enum AuthOp { enrollment, authentication }

/// Defines a service that performs authentication tasks like:
/// - create an account with password
/// - login to an account with password
/// - logout from an account
///
/// Note:
/// - It always picks the first account for login and logout.
/// - Creating an account, when an account already exists, is an undefined
///   behavior. The client of the service should ensure to not call account
///   creation in this case.
class AuthService {
  late final PseudoDir hostedDirectories;

  final _accountManager = AccountManagerProxy();
  AccountProxy? _account;
  final _accountIds = <int>[];
  final _ready = false.asObservable();

  /// Set to true if successfully authenticated, false otherwise or post logout.
  bool authenticated = false;

  AuthService() {
    Incoming.fromSvcPath().connectToService(_accountManager);
  }

  void dispose() {
    _accountManager.ctrl.close();
  }

  /// Load existing accounts from [AccountManager].
  void loadAccounts(AuthMode currentAuthMode) async {
    try {
      final ids = (await _accountManager.getAccountIds()).toList();

      // TODO(http://fxb/85576): Remove once login and OOBE are mandatory.
      // Remove any accounts created with a deprecated name or from an auth
      // mode that does not match the current build configuration.
      final tempIds = <int>[]..addAll(ids);
      for (var id in tempIds) {
        final metadata = await _accountManager.getAccountMetadata(id);

        if (metadata.name != null &&
            _shouldRemoveAccountWithName(metadata.name!, currentAuthMode)) {
          try {
            await _accountManager.removeAccount(id);
            ids.remove(id);
            log.info('Removed account: $id with name: ${metadata.name}');
            // ignore: avoid_catches_without_on_clauses
          } catch (e) {
            // We can only log and continue.
            log.shout('Failed during deprecated account removal: $e');
          }
        }
      }
      _accountIds.addAll(ids);
      runInAction(() => _ready.value = true);
      if (ids.length > 1) {
        log.shout(
            'Multiple (${ids.length}) accounts found, will use the first.');
      }
      // ignore: avoid_catches_without_on_clauses
    } catch (e) {
      log.shout('Failed during deprecated account removal: $e');
    }
  }

  bool _shouldRemoveAccountWithName(String name, AuthMode currentAuthMode) {
    if (name == kDeprecatedAccountName) {
      return true;
    }
    if (currentAuthMode == AuthMode.automatic) {
      // Current auth is automatic, remove account with user picked name.
      return name == kUserPickedAccountName;
    } else {
      // Current auth is manual, remove account with system picked name.
      return name == kSystemPickedAccountName;
    }
  }

  /// Calls [FactoryReset] service to factory data reset the device.
  void factoryReset() {
    final proxy = FactoryResetProxy();
    Incoming.fromSvcPath().connectToService(proxy);
    proxy
        .reset()
        .then((status) => log.info('Requested factory reset.'))
        .catchError((e) => log.shout('Failed to factory reset device: $e'));
  }

  /// Returns [true] after [_accountManager.getAccountIds()] completes.
  bool get ready => _ready.value;

  /// Returns [true] if no accounts exists on device.
  bool get hasAccount {
    assert(ready, 'Called before list of accounts could be retrieved.');
    return _accountIds.isNotEmpty;
  }

  String errorFromException(Object e, AuthOp op) {
    if (e is MethodException) {
      switch (e.value as Error) {
        case Error.failedAuthentication:
          return Strings.accountPasswordFailedAuthentication;
        case Error.notFound:
          switch (op) {
            case AuthOp.authentication:
              return Strings.accountNotFound;
            case AuthOp.enrollment:
              return Strings.accountPartitionNotFound;
          }
      }
    }
    return e.toString();
  }

  /// Creates an account with password and sets up the account data directory.
  Future<void> createAccountWithPassword(String password) async {
    assert(_account == null, 'An account already exists.');
    if (_account != null && _account!.ctrl.isBound) {
      // ignore: unawaited_futures
      _account!.lock().catchError((_) {});
      _account!.ctrl.close();
    }

    final metadata = AccountMetadata(
        name: password.isEmpty
            ? kSystemPickedAccountName
            : kUserPickedAccountName);
    _account = AccountProxy();
    await _accountManager.deprecatedProvisionNewAccount(
      password,
      metadata,
      _account!.ctrl.request(),
    );
    final ids = await _accountManager.getAccountIds();
    _accountIds
      ..clear()
      ..addAll(ids);
    log.info('Account creation succeeded.');

    await _publishAccountDirectory(_account!);

    authenticated = true;
  }

  /// Logs in to the first account with [password] and sets up the account data
  /// directory.
  Future<void> loginWithPassword(String password) async {
    assert(_accountIds.isNotEmpty, 'No account exist to login to.');
    if (_account != null && _account!.ctrl.isBound) {
      // ignore: unawaited_futures
      _account!.lock().catchError((_) {});
      _account!.ctrl.close();
    }

    _account = AccountProxy();
    await _accountManager.deprecatedGetAccount(
      _accountIds.first,
      password,
      _account!.ctrl.request(),
    );
    log.info('Login to first account on device succeeded.');

    await _publishAccountDirectory(_account!);

    authenticated = true;
  }

  /// Logs out of an account by locking it and deleting the associated tmp
  /// directory.
  Future<void> logout() async {
    assert(_account != null, 'No account exists to logout from.');

    log.info('Locking account.');
    await _account!.lock();

    authenticated = false;

    // We expect the tmp subdirectory should exist by logout, but if it
    // doesn't then we can just continue without attempting deletion.
    if (io.Directory('$kIncomingTmpDirectory$kTmpSubdirectory').existsSync()) {
      log.info('Deleting tmp directory for account.');
      await io.Directory('$kIncomingTmpDirectory$kTmpSubdirectory')
          .delete(recursive: true);
    }
  }

  /// Publishes all flavors of storage directory for the supplied account.
  Future<void> _publishAccountDirectory(Account account) async {
    // Get the data directory for the account.
    log.info('Getting data directory for account.');
    final dataDirChannel = ChannelPair();
    await account.getDataDirectory(InterfaceRequest(dataDirChannel.second));

    // Open or create a subdirectory for the cache storage capability.
    log.info('Opening cache directory for account.');
    final dataDir = RemoteDir(dataDirChannel.first!);
    final cacheSubdirChannel = ChannelPair();
    dataDir.open(
        OpenFlags.rightReadable |
            OpenFlags.rightWritable |
            OpenFlags.create |
            OpenFlags.directory,
        0,
        kCacheSubdirectory,
        InterfaceRequest(cacheSubdirChannel.second));

    // Create a directory for the tmp storage capability.
    log.info('Creating tmp directory for account.');
    final tmpDir = RemoteDir(Channel.fromFile(kIncomingTmpDirectory));
    final tmpSubdirChannel = ChannelPair();
    tmpDir.open(
        OpenFlags.rightReadable |
            OpenFlags.rightWritable |
            OpenFlags.create |
            OpenFlags.directory,
        0,
        kTmpSubdirectory,
        InterfaceRequest(tmpSubdirChannel.second));

    // Host all directories.
    hostedDirectories
      ..removeNode(kAccountDataDirectory)
      ..addNode(kAccountDataDirectory, dataDir)
      ..removeNode(kAccountCacheDirectory)
      ..addNode(kAccountCacheDirectory, RemoteDir(cacheSubdirChannel.first!))
      ..removeNode(kAccountTmpDirectory)
      ..addNode(kAccountTmpDirectory, RemoteDir(tmpSubdirChannel.first!));

    log.info('Data, cache, and tmp directories for account published.');
  }
}

// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:fidl/fidl.dart';
import 'package:ermine_utils/ermine_utils.dart';
import 'package:fidl_fuchsia_identity_account/fidl_async.dart';
import 'package:fuchsia_logger/logger.dart';
import 'package:fuchsia_services/services.dart';
import 'package:fuchsia_vfs/vfs.dart';
import 'package:zircon/zircon.dart';

const kAccountName = 'created_by_user';
const kAccountDirectory = 'account_data';

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
  late final Outgoing outgoing;

  final _accountManager = AccountManagerProxy();
  AccountProxy? _account;
  final _accountIds = <int>[];
  final _ready = false.asObservable();

  AuthService() {
    // Connect to AccountManager and get list of all account ids.
    Incoming.fromSvcPath().connectToService(_accountManager);
    _accountManager.getAccountIds().then((ids) {
      _ready.value = true;
      if (ids.length > 1) {
        log.shout(
            'Multiple (${ids.length}) accounts found, will use the first.');
      } else {
        _accountIds.addAll(ids);
      }
    });
  }

  void dispose() {
    _accountManager.ctrl.close();
  }

  /// Returns [true] after [_accountManager.getAccountIds()] completes.
  bool get ready => _ready.value;

  /// Returns [true] if not accounts exists on device.
  bool get hasAccount {
    assert(ready, 'Called before list of accounts could be retrieved.');
    return _accountIds.isNotEmpty;
  }

  /// Creates an account with password and sets up the account data directory.
  Future<void> createAccountWithPassword(String password) async {
    assert(_accountIds.isEmpty, 'An account already exists.');
    _account?.ctrl.close();

    final metadata = AccountMetadata(name: kAccountName);
    _account = AccountProxy();
    await _accountManager.deprecatedProvisionNewAccount(
      password,
      metadata,
      _account!.ctrl.request(),
    );
    log.info('Account creation succeeded.');

    await _publishAccountDirectory(_account!);
  }

  /// Logs in to the first account with [password] and sets up the account data
  /// directory.
  Future<void> loginWithPassword(String password) async {
    assert(_accountIds.isNotEmpty, 'No account exist to login to.');
    _account?.ctrl.close();

    _account = AccountProxy();
    await _accountManager.deprecatedGetAccount(
      _accountIds.first,
      password,
      _account!.ctrl.request(),
    );
    log.info('Login to first account on device succeeded.');

    await _publishAccountDirectory(_account!);
  }

  /// Logs out of an account by locking it.
  Future<void> logout() async {
    assert(_accountIds.isNotEmpty || _account == null,
        'No account exist to logout from.');

    return _account!.lock();
  }

  Future<void> _publishAccountDirectory(Account account) async {
    // Get the data directory for the account.
    log.info('Getting data directory for account.');
    final directory = ChannelPair();
    await account.getDataDirectory(InterfaceRequest(directory.second));

    outgoing.rootDir()
      ..removeNode(kAccountDirectory)
      ..addNode(kAccountDirectory, RemoteDir(directory.first!));

    log.info('Data directory for account published.');
  }
}

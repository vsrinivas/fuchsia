import 'dart:async';

import 'package:fidl_fuchsia_modular/fidl_async.dart';
import 'package:fidl_fuchsia_modular_auth/fidl_async.dart';
import 'package:fuchsia_logger/logger.dart';

/// Handles adding, removing, and logging, and controlling users.
class BaseShellUserManager {
  final UserProvider _userProvider;

  final StreamController<void> _userLogoutController =
      StreamController<void>.broadcast();

  BaseShellUserManager(this._userProvider);

  Stream<void> get onLogout => _userLogoutController.stream;

  /// Adds a new user, displaying UI as required.
  ///
  /// The UI will be displayed in the space provided to authenticationContext
  /// in the base shell widget.
  Future<String> addUser() {
    final completer = Completer<String>();

    _userProvider.addUser(IdentityProvider.google).then((response) {
      if (response.errorCode == null || response.errorCode == '') {
        completer.complete(response.account.id);
      } else {
        log.warning('ERROR adding user!  ${response.errorCode}');
        completer
            .completeError(UserLoginException('addUser', response.errorCode));
      }
    });

    return completer.future;
  }

  /// Logs in the user given by [accountId].
  void login(String accountId) {
    final UserLoginParams params = UserLoginParams(
      accountId: accountId,
    );

    _userProvider.login(params);
  }

  Future<void> removeUser(String userId) {
    final completer = Completer<void>();

    _userProvider.removeUser(userId).then((errorCode) {
      if (errorCode != null && errorCode != '') {
        completer
            .completeError(UserLoginException('removing $userId', errorCode));
      }
      completer.complete();
    });

    return completer.future;
  }

  /// Gets the list of accounts already logged in.
  Future<Iterable<Account>> getPreviousUsers() {
    final completer = Completer<Iterable<Account>>();

    _userProvider.previousUsers().then(completer.complete);

    return completer.future;
  }

  void close() {
    _userLogoutController.close();
  }
}

/// Exception thrown when performing user management operations.
class UserLoginException implements Exception {
  final String errorCode;
  final String operation;

  UserLoginException(this.operation, this.errorCode);

  @override
  String toString() {
    return 'Failed during $operation: $errorCode';
  }
}

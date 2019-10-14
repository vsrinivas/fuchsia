// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';

import 'package:fidl/fidl.dart';
import 'package:fidl_fuchsia_update/fidl_async.dart' as update;
import 'package:fidl_fuchsia_pkg/fidl_async.dart' as pkg;
import 'package:fidl_fuchsia_pkg_rewrite/fidl_async.dart' as pkg_rewrite;
import 'package:fidl_fuchsia_recovery/fidl_async.dart' as recovery;
import 'package:flutter/foundation.dart';
import 'package:fuchsia_logger/logger.dart';
import 'package:fuchsia_services/services.dart';
import 'package:lib.settings/device_info.dart';
import 'package:lib.widgets/model.dart';
import 'package:zircon/zircon.dart';

/// Clock ID of the system monotonic clock, which measures uptime in nanoseconds.
const int _zxClockMonotonic = 0;

const Duration _uptimeRefreshInterval = Duration(seconds: 1);

/// An interface for abstracting system interactions.
abstract class SystemInterface {
  int get currentTime;

  Future<bool> checkForSystemUpdate();

  Stream<pkg.RepositoryConfig> listRepositories();

  Stream<pkg_rewrite.Rule> listRules();

  Stream<pkg_rewrite.Rule> listStaticRules();

  Future<int> updateRules(
      Iterable<pkg_rewrite.Rule> Function(List<pkg_rewrite.Rule>) action);

  Future<void> factoryReset();

  void dispose();
}

class DefaultSystemInterfaceImpl implements SystemInterface {
  /// Controller for the update manager service.
  final update.ManagerProxy _updateManager = update.ManagerProxy();

  /// Controller for package repo manager and rewrite engine (our update service).
  final pkg.RepositoryManagerProxy _repositoryManager =
      pkg.RepositoryManagerProxy();
  final pkg_rewrite.EngineProxy _rewriteManager = pkg_rewrite.EngineProxy();

  /// Controller for the factory reset service.
  final recovery.FactoryResetProxy _factoryReset = recovery.FactoryResetProxy();

  DefaultSystemInterfaceImpl() {
    StartupContext.fromStartupInfo().incoming.connectToService(_updateManager);
    StartupContext.fromStartupInfo()
        .incoming
        .connectToService(_repositoryManager);
    StartupContext.fromStartupInfo().incoming.connectToService(_rewriteManager);
  }

  @override
  int get currentTime => System.clockGet(_zxClockMonotonic) ~/ 1000;

  @override
  Future<bool> checkForSystemUpdate() async {
    final options = update.Options(
      initiator: update.Initiator.user,
    );
    final status = await _updateManager.checkNow(options, null);
    return status != update.CheckStartedResult.throttled;
  }

  @override
  Stream<pkg.RepositoryConfig> listRepositories() async* {
    final iter = pkg.RepositoryIteratorProxy();
    await _repositoryManager.list(iter.ctrl.request());

    List<pkg.RepositoryConfig> repos;
    do {
      repos = await iter.next();
      for (var repo in repos) {
        yield repo;
      }
    } while (repos.isNotEmpty);
  }

  @override
  Stream<pkg_rewrite.Rule> listRules() {
    return _listRules(_rewriteManager.list);
  }

  @override
  Stream<pkg_rewrite.Rule> listStaticRules() {
    return _listRules(_rewriteManager.listStatic);
  }

  Future<int> editRuleTransaction(
      Future<void> Function(pkg_rewrite.EditTransaction) action) async {
    // In most cases this loop will only run once, but to be safe against
    // other writers we loop a few times.
    for (var i = 0; i < 10; i++) {
      final transaction = pkg_rewrite.EditTransactionProxy();
      await _rewriteManager.startEditTransaction(transaction.ctrl.request());
      await action(transaction);
      final status = await transaction.commit();
      switch (status) {
        case ZX.OK:
          return ZX.OK;
        case ZX.ERR_UNAVAILABLE:
          continue;
        default:
          return status;
      }
    }
    return ZX.ERR_UNAVAILABLE;
  }

  @override
  Future<int> updateRules(
      Iterable<pkg_rewrite.Rule> Function(List<pkg_rewrite.Rule>)
          action) async {
    return editRuleTransaction((tx) async {
      final rules = <pkg_rewrite.Rule>[];
      await _listRules(tx.listDynamic).forEach(rules.add);

      await tx.resetAll();

      for (var rule in action(rules).toList().reversed) {
        await tx.add(rule);
      }
    });
  }

  @override
  Future<void> factoryReset() async {
    if (_factoryReset.ctrl.isUnbound) {
      StartupContext.fromStartupInfo().incoming.connectToService(_factoryReset);
    }

    await _factoryReset.reset();
  }

  @override
  void dispose() {
    _updateManager.ctrl.close();
    _repositoryManager.ctrl.close();
    _rewriteManager.ctrl.close();
    _factoryReset.ctrl.close();
  }
}

/// Model containing state needed for the device settings app.
class DeviceSettingsModel extends Model {
  /// Placeholder time of last update, used to provide visual indication update
  /// was called.
  ///
  /// This will be removed when we have a more reliable way of showing update
  /// status.
  /// TODO: replace with better status info from update service
  DateTime _lastUpdate;

  /// Holds the build tag if a release build, otherwise holds
  /// the time the source code was updated.
  String _buildTag;

  /// Holds the time the source code was updated.
  String _sourceDate;

  /// Length of time since system bootup.
  Duration _uptime;
  Timer _uptimeRefreshTimer;

  bool _started = false;

  bool _showResetConfirmation = false;

  ValueNotifier<bool> channelPopupShowing = ValueNotifier<bool>(false);

  final List<pkg.RepositoryConfig> _repos = [];
  final List<pkg_rewrite.Rule> _rules = [];
  final List<pkg_rewrite.Rule> _staticRules = [];

  bool _isChannelUpdating = false;

  SystemInterface _sysInterface;

  DeviceSettingsModel(this._sysInterface);

  DeviceSettingsModel.withDefaultSystemInterface()
      : this(DefaultSystemInterfaceImpl());

  DateTime get lastUpdate => _lastUpdate;

  List<pkg.RepositoryConfig> get repos => _repos;

  List<pkg_rewrite.Rule> get rules => _rules;

  String get currentChannel {
    final rule =
        rules.firstWhere(_ruleIsFuchsiaReplacement, orElse: () => null);

    return rule?.literal?.hostReplacement;
  }

  String get defaultChannel {
    final rule =
        _staticRules.firstWhere(_ruleIsFuchsiaReplacement, orElse: () => null);

    return rule?.literal?.hostReplacement;
  }

  /// Determines whether the confirmation dialog for factory reset should
  /// be displayed.
  bool get showResetConfirmation => _showResetConfirmation;

  /// Returns true if we are in the middle of updating the channel. Returns
  /// false otherwise.
  bool get channelUpdating => _isChannelUpdating;

  String get buildTag => _buildTag;

  String get sourceDate => _sourceDate;

  Duration get uptime => _uptime;

  bool get updateCheckDisabled =>
      DateTime.now().isAfter(_lastUpdate.add(Duration(seconds: 60)));

  /// Checks for update from the update service
  Future<void> checkForUpdates() async {
    await _sysInterface.checkForSystemUpdate();
    _lastUpdate = DateTime.now();
  }

  Future<void> selectChannel(pkg.RepositoryConfig selectedConfig) async {
    log.info('selecting channel ${selectedConfig.repoUrl}');
    channelPopupShowing.value = false;
    _setChannelState(updating: true);

    final repoUrl = Uri.parse(selectedConfig.repoUrl);
    final pkg_rewrite.Rule selectedRule = pkg_rewrite.Rule.withLiteral(
        pkg_rewrite.LiteralRule(
            hostMatch: 'fuchsia.com',
            hostReplacement: repoUrl.host,
            pathPrefixMatch: '/',
            pathPrefixReplacement: '/'));

    final status = await _sysInterface.updateRules((rules) sync* {
      // Find the first fuchsia.com rule. If it doesn't exist, add it to the
      // front of the rule set. Otherwise replace it, and filter out any
      // duplicates.
      final index = rules.indexWhere(_ruleIsFuchsiaReplacement);
      if (index == -1) {
        yield selectedRule;
        yield* rules;
      } else {
        yield* rules.getRange(0, index);
        yield selectedRule;
        yield* rules
            .getRange(index + 1, rules.length)
            .where((rule) => !_ruleIsFuchsiaReplacement(rule));
      }
    });

    if (status != ZX.OK) {
      log.severe('error while modifying rewrite rules: $status');
    }

    await _update();
  }

  Future<void> clearChannel() async {
    log.info('clearing channel');
    channelPopupShowing.value = false;
    _setChannelState(updating: true);

    final status = await _sysInterface.updateRules(
        (rules) => rules.where((rule) => !_ruleIsFuchsiaReplacement(rule)));

    if (status != ZX.OK) {
      log.severe('error while modifying rewrite rules: $status');
    }

    await _update();
  }

  Future<void> _update() async {
    _setChannelState(updating: true);

    _repos.clear();
    await _sysInterface.listRepositories().forEach(_repos.add);

    _rules.clear();
    await _sysInterface.listRules().forEach(_rules.add);

    _staticRules.clear();
    await _sysInterface.listStaticRules().forEach(_staticRules.add);

    _setChannelState(updating: false);
  }

  void _setChannelState({bool updating}) {
    if (_isChannelUpdating == updating) {
      return;
    }

    _isChannelUpdating = updating;
    notifyListeners();
  }

  void dispose() {
    _sysInterface.dispose();
    _uptimeRefreshTimer.cancel();
  }

  Future<void> start() async {
    if (_started) {
      return;
    }

    _started = true;
    _buildTag = DeviceInfo.buildTag;
    _sourceDate = DeviceInfo.sourceDate;

    updateUptime();
    _uptimeRefreshTimer =
        Timer.periodic(_uptimeRefreshInterval, (_) => updateUptime());

    await _update();

    channelPopupShowing.addListener(notifyListeners);
  }

  void updateUptime() {
    // System clock returns time since boot in nanoseconds.
    _uptime = Duration(microseconds: _sysInterface.currentTime);
    notifyListeners();
  }

  Future<void> factoryReset() async {
    if (showResetConfirmation) {
      log.warning('Triggering factory reset');
      await _sysInterface.factoryReset();
    } else {
      _showResetConfirmation = true;
      notifyListeners();
    }
  }

  void cancelFactoryReset() {
    _showResetConfirmation = false;
    notifyListeners();
  }
}

Stream<pkg_rewrite.Rule> _listRules(
    Future<void> Function(InterfaceRequest<pkg_rewrite.RuleIterator>)
        action) async* {
  final iter = pkg_rewrite.RuleIteratorProxy();
  await action(iter.ctrl.request());

  List<pkg_rewrite.Rule> rules;
  do {
    rules = await iter.next();
    for (var rule in rules) {
      yield rule;
    }
  } while (rules.isNotEmpty);
}

bool _ruleIsFuchsiaReplacement(pkg_rewrite.Rule rule) {
  return rule.literal != null &&
      rule.literal.hostMatch == 'fuchsia.com' &&
      rule.literal.pathPrefixMatch == '/';
}

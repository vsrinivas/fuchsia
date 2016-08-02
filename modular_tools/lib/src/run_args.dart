// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:args/args.dart' show ArgParser, ArgResults;

import 'configuration.dart';

const String _kDefaultRootURL = 'https://tq.mojoapps.io/';
const String _kLedgerURL = _kDefaultRootURL + 'ledger.mojo';
const String _kHandlerURL = _kDefaultRootURL + 'handler.mojo';
const String _kSyncbaseSyncedStoreURL =
    _kDefaultRootURL + 'syncbase_synced_store.mojo';

/// Adds command line params that determine common Mojo args for a run of
/// Modular.
void addMojoOptions(final ArgParser argParser) {
  argParser.addFlag('deployed',
      negatable: false, help: 'To use the deployed version of modular');
  argParser.addFlag('offline',
      negatable: false,
      help: 'Use ledger in offline mode, which disables sync');
  argParser.addOption('embodiment',
      help: 'The embodiment string to be used for the root module.');
  argParser.addFlag('sync',
      defaultsTo: false,
      negatable: true,
      help: 'Use a backend for ledger instead of in memory.');
  argParser.addFlag('create-user-manager-graph',
      negatable: false,
      help: 'Creates the user manager graph in the ledger. This graph is '
          'expected to be created only once, and should not be launhed '
          'multiple times. Creating this graph multiple times, would make the'
          ' syncbase data to enter into wierd state, where all subsequent '
          'laucnhes would start crashing. PLEASE use it only when syncbase in'
          ' the cloud is started fresh with out any data.');
  argParser.addOption('device-id',
      help: 'Device to route all adb communication');
  argParser.addFlag('reuse-servers',
      negatable: false, help: 'Reuse servers in multi device setup');
  argParser.addFlag('verbose',
      negatable: false, help: 'Print more debug information.');
}

/// Returns a list of Mojo args to set when running Modular. These are shared
/// between various contexts in which Modular runs, e.g. `run`, `test`.
List<String> getMojoArgs(
    final ArgResults argResults, final TargetPlatform target, bool isTest) {
  final List<String> args = [];
  final String targetPlatformString =
      target.toString().substring(target.toString().indexOf('.') + 1);

  String ledgerArgs = '--args-for=$_kLedgerURL --$targetPlatformString';
  String syncStoreArgs =
      '--args-for=$_kSyncbaseSyncedStoreURL --$targetPlatformString';
  String hanlderArgs = '--args-for=$_kHandlerURL';

  bool isOffline = isTest || argResults['offline'];

  // The subsequent args are not parsed by modular_tools run, but go on to
  // the Mojo Shell.

  if (argResults['deployed']) {
    args.add('--no-config-file');
  }

  if (isTest) {
    ledgerArgs += ' --mock';
  }

  if (isOffline) {
    ledgerArgs += ' --offline';
    syncStoreArgs += ' --offline';
  }

  if (argResults['sync']) {
    hanlderArgs += ' --sync';
  }

  String rootEmbodiment = argResults['embodiment'];
  if (rootEmbodiment != null) {
    hanlderArgs += ' --embodiment=$rootEmbodiment';
  }

  args.add(hanlderArgs);
  args.add(ledgerArgs);
  args.add(syncStoreArgs);

  if (argResults['create-user-manager-graph']) {
    args.add('--args-for=$_kHandlerURL --create-user-manager-graph');
  }

  if (!isTest) {
    final String deviceId = argResults['device-id'];
    if (deviceId != null) {
      args.add('--target-device=$deviceId');
    }
    if (argResults['reuse-servers']) {
      args.add('--reuse-servers');
    }
  }

  if (argResults['verbose']) {
    args.add('--verbose');
  }

  return args;
}

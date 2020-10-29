// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// @dart = 2.8

import 'dart:async';
import 'dart:core';
import 'dart:io';

import 'package:args/args.dart';
import 'package:fuchsia_remote_debug_protocol/fuchsia_remote_debug_protocol.dart';
import 'package:fuchsia_remote_debug_protocol/logging.dart';

const String kIpAddrFlag = 'ip-address';
const String kNetIfaceFlag = 'network-interface';
const String kSshConfigFlag = 'ssh-config';
const String kIsolateNameFlag = 'isolate';
const String kVerboseFlag = 'verbose';

/// Utility function: returns `true` if `flag` is neither null nor empty.
bool flagIsValid(String flag) {
  return flag != null && !flag.isEmpty;
}

Future<Null> main(List<String> args) async {
  final ArgParser parser = new ArgParser()
    ..addFlag(kVerboseFlag, defaultsTo: false, negatable: false)
    ..addOption(kIpAddrFlag)
    ..addOption(kNetIfaceFlag)
    ..addOption(kSshConfigFlag)
    ..addOption(kIsolateNameFlag);
  final ArgResults results = parser.parse(args);
  // Since this is being run from a parent script, just return an error instead
  // of printing help text, as extra help text would be confusing.
  if (!flagIsValid(results[kIpAddrFlag]) ||
      !flagIsValid(results[kSshConfigFlag]) ||
      !flagIsValid(results[kNetIfaceFlag])) {
    exit(1);
  }
  if (results[kVerboseFlag]) {
    Logger.globalLevel = LoggingLevel.all;
  }
  final String ipAddress = results[kIpAddrFlag];
  final String sshConfigFlag = results[kSshConfigFlag];
  final String netInterfaceFlag = results[kNetIfaceFlag];
  print('Connecting to device at ${ipAddress} . . .');
  final FuchsiaRemoteConnection connection =
      await FuchsiaRemoteConnection.connect(
    ipAddress,
    netInterfaceFlag.isEmpty ? null : netInterfaceFlag,
    sshConfigFlag,
  );
  final String isolateName = results[kIsolateNameFlag];
  final Pattern isolatePattern = flagIsValid(isolateName) ? isolateName : r'';
  final List<IsolateRef> isolates =
      await connection.getMainIsolatesByPattern(isolatePattern);
  final String plural =
      isolates.length == 0 || isolates.length > 1 ? 'isolates' : 'isolate';
  final String isolateResultString = 'Found ${isolates.length} $plural';
  print(isolateResultString);
  // Creates a fancy dotted line under the result string.
  print(new String.fromCharCodes(
      isolateResultString.codeUnits.map((int codeUnit) => '-'.codeUnitAt(0))));
  for (IsolateRef ref in isolates) {
    // Replace the websocket protocol with http for browser-friendly links.
    final Uri dartVmUri = ref.dartVm.uri.replace(scheme: 'http', path: '');
    print('${ref.name}: $dartVmUri');
  }

  ProcessSignal.SIGINT.watch().listen((ProcessSignal signal) async {
    print('');
    print('SIGINT received. Shutting down.');
    await connection.stop();
    exit(0);
  });
  print('<Press Ctrl-C to exit>');
}

// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
import 'dart:convert';
import 'dart:core';
import 'dart:io';

import 'exceptions.dart';
import 'runner.dart';

/// Wrapper for the ffx binary.
class Ffx {
  /// The name of the target device.
  final String nodename;
  final FfxRunner _runner;

  Ffx(this.nodename, this._runner);

  /// Constructs an [Ffx] client from the following environment variables:
  ///
  /// * `FUCHSIA_NODENAME`:
  ///     The name of the target device.
  /// * `FUCHSIA_DEVICE_ADDR`, `FUCHSIA_IPV4_ADDR`, or `FUCHSIA_IPV6_ADDR`:
  ///     The IPV4 or IPV6 address of the target.
  ///
  /// The `FUCHSIA_NODENAME` will be checked first and used if present.
  /// Otherwise, the target nodename will be discovered using the
  /// `FUCHSIA_DEVICE_ADDR`, `FUCHSIA_IPV4_ADDR`, or `FUCHSIA_IPV6_ADDR`
  /// environment variables. Either an explicit name or an address is required.
  static Future<Ffx> fromEnvironment(
      {Map<String, String>? environment, FfxRunner? runner}) async {
    runner ??= FfxRunner();
    environment ??= Platform.environment;

    final nodename = environment['FUCHSIA_NODENAME'] ?? '';
    if (nodename.isNotEmpty) {
      return Ffx(nodename, runner);
    }

    final address = environment['FUCHSIA_DEVICE_ADDR'] ??
        environment['FUCHSIA_IPV4_ADDR'] ??
        environment['FUCHSIA_IPV6_ADDR'] ??
        '';
    if (address.isEmpty) {
      throw FfxException(
          'No FUCHSIA_NODENAME, FUCHSIA_DEVICE_ADDR, FUCHSIA_IPV4_ADDR, '
          'or FUCHSIA_IPV6_ADDR provided when creating Ffx from environment');
    }

    return Ffx.fromAddress(address, runner: runner);
  }

  /// Constructs an [Ffx] client from a Fuchsia target IPV4 or IPV6 `address`.
  static Future<Ffx> fromAddress(String address, {FfxRunner? runner}) async {
    runner ??= FfxRunner();
    try {
      final stdout =
          await runner.runWithOutput(['target', 'list', '--format', 'json']);
      final targets = json.decode(await stdout.join('\n'));
      final node = targets.firstWhere(
          (target) => List<String>.from(target['addresses']).contains(address),
          orElse: () => throw FfxException(
              'Target with address does not exist: $address'));
      final nodename = node['nodename'] ??
          (throw FfxException('ffx target list output is malformed'));
      return Ffx(nodename, runner);
    } on FfxException {
      rethrow;
    } on Exception catch (e) {
      throw FfxException('Failed to construct client from address: $e');
    }
  }

  List<String> _argsWithTarget(List<String> args) =>
      ['--target', nodename] + args;

  Future<Process> run(List<String> args) async =>
      _runner.run(_argsWithTarget(args));
}

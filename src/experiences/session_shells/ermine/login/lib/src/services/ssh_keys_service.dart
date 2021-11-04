// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';
import 'dart:convert';
import 'dart:io';

import 'package:fidl_fuchsia_ssh/fidl_async.dart';
import 'package:fuchsia_logger/logger.dart';
import 'package:fuchsia_services/services.dart';
import 'package:internationalization/strings.dart';

/// Defines a service to load ssh keys from Github and save them to device.
class SshKeysService {
  final _proxy = AuthorizedKeysProxy();

  SshKeysService() {
    Incoming.fromSvcPath().connectToService(_proxy);
  }

  Future<void> addKey(String key) {
    final entry = SshAuthorizedKeyEntry(key: key);
    return _proxy.addKey(entry);
  }

  void dispose() {
    _proxy.ctrl.close();
  }

  /// Returns list of github keys for the specified [username].
  Future<List<String>> fetchKeys(String username) async {
    String path = '/users/$username/keys';
    final client = HttpClient();
    final request = await client.getUrl(Uri.https('api.github.com', path));
    final response = await request.close();

    if (response.statusCode == 200) {
      final completer = Completer<String>();
      final contents = StringBuffer();
      response.transform(utf8.decoder).listen(contents.write,
          onDone: () => completer.complete(contents.toString()));

      final data = jsonDecode(await completer.future);
      List<String> keys = [];
      for (final item in data) {
        keys.add(item['key']);
      }
      return keys;
    } else {
      // A 404 response indicates that the user does not exist or has no public
      // keys.
      if (response.statusCode != 404) {
        log.info(
            'OOBE: request to get keys from github returned ${response.statusCode}: ${response.reasonPhrase}.');
        throw Exception(Strings.oobeSshKeysHttpErrorDesc(
            response.statusCode, response.reasonPhrase));
      }
      return [];
    }
  }
}

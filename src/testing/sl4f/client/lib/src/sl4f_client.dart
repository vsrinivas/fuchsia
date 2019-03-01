// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file is named sl4f_client.dart since the top-level dart file is names
// sl4f.dart, which would cause problems for src files trying to import this
// file.

import 'dart:convert';
import 'dart:io' show Platform;

import 'package:http/http.dart' as http;

/// Wraps the error field of a JSON RPC as an [Exception].
class JsonRpcException implements Exception {
  JsonRpcException(this.error);
  final dynamic error;

  @override
  String toString() {
    return 'JSON RPC returned error: $error';
  }
}

class Sl4f {
  final _client = http.Client();
  String host;

  Sl4f(this.host) {
    print('Target host: $host');
  }

  /// Constructs an SL4F client from the IP_ADDR environment variable.
  Sl4f.fromEnvironment() : this(Platform.environment['IP_ADDR']);

  /// Closes the underlying HTTP client. This need not be called if the
  /// Sl4f client is closed instead.
  void close() {
    _client.close();
  }

  Future<dynamic> request(String method, [dynamic params]) async {
    // Although params is optional, this will pass a null params if it is
    // omitted. This is actually required by our SL4F server (although it is
    // not required in JSON RPC:
    // https://www.jsonrpc.org/specification#request_object).
    final httpRequest = http.Request('GET', Uri.http(host, ''))
      ..body = jsonEncode({'id': '', 'method': method, 'params': params});

    final httpResponse =
        await http.Response.fromStream(await _client.send(httpRequest));
    Map<String, dynamic> response = jsonDecode(httpResponse.body);
    final dynamic error = response['error'];
    if (error != null) {
      throw JsonRpcException(error);
    }

    return response['result'];
  }
}

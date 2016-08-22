// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';
import 'dart:typed_data';

import 'package:mojo/core.dart';
import 'package:mojo/mojo/url_request.mojom.dart';
import 'package:mojo/mojo/http_header.mojom.dart' as http_header_mojom;
import 'package:mojo_services/mojo/network_service.mojom.dart';
import 'package:mojo_services/mojo/url_loader.mojom.dart';

import 'uri_loader.dart';
import 'package:mojo/mojo/url_response.mojom.dart';

// Very primitive functionality for loading an URI from the network.
// TODO(tonyg): Remove this when we're able to use 'dart:io'.
class MojoUriLoader implements UriLoader {
  final NetworkServiceProxy _networkService = new NetworkServiceProxy.unbound();

  MojoUriLoader(Function connectToService) {
    if (connectToService != null) {
      connectToService("mojo:authenticated_network_service", _networkService);
    }
  }

  /// Returns the string contents of [uri] or null upon error.
  @override
  Future<String> getString(final Uri uri, {final Map<String, String> headers}) {
    final UrlLoaderProxy urlLoaderProxy = new UrlLoaderProxy.unbound();
    _networkService.createUrlLoader(urlLoaderProxy);

    final UrlRequest urlRequest = new UrlRequest()
      ..url = uri.toString()
      ..autoFollowRedirects = true;

    if (headers != null) {
      final List<http_header_mojom.HttpHeader> httpHeaders = [];
      headers.forEach((String key, String value) {
        var httpHeader = new http_header_mojom.HttpHeader()
          ..name = key
          ..value = value;
        httpHeaders.add(httpHeader);
      });
      urlRequest.headers = httpHeaders;
    }

    final Completer<String> completer = new Completer<String>();

    urlLoaderProxy.start(urlRequest, (final UrlResponse urlResponse) async {
      if (urlResponse.statusCode != 200) {
        print("ERROR: Received a ${urlResponse.statusCode} for $uri.");
        completer.complete();
      } else {
        final ByteData body =
            await DataPipeDrainer.drainHandle(urlResponse.body);
        completer.complete(
            new String.fromCharCodes(new Uint8List.view(body.buffer)));
      }
      urlLoaderProxy.close();
    });
    return completer.future;
  }

  Future<Null> close() {
    return _networkService.close();
  }
}

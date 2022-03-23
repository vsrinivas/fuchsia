// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'realm_builder.dart';

class RealmBuilderError implements Exception {
  String cause;

  RealmBuilderError(this.cause);

  @override
  String toString() {
    return '$runtimeType("$cause")';
  }
}

class MissingSource extends RealmBuilderError {
  MissingSource() : super('route is missing source');
}

class RefUsedInWrongRealmException extends RealmBuilderError {
  Ref ref;
  RefUsedInWrongRealmException(this.ref, String realmPath)
      : super("unable to use reference '$ref' in realm '$realmPath'");
}

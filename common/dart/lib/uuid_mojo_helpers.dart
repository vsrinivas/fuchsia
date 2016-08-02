// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:modular_services/common/uuid.mojom.dart' as mojom_uuid;
import 'package:modular_core/uuid.dart';

/// [UuidMojoHelpers] provides utility functions to deal with the mojom Uuid
/// structure in Dart code.
class UuidMojoHelpers {
  /// Returns a common [Uuid] instance based on the given mojom Uuid
  /// structure.
  static Uuid fromMojom(final mojom_uuid.Uuid mojomUuid) {
    return new Uuid(mojomUuid.value);
  }

  /// Converts the given [Uuid] structure to a mojom structure that can be used
  /// in mojo transactions.
  static mojom_uuid.Uuid toMojom(final Uuid uuid) {
    return new mojom_uuid.Uuid()..value = uuid.data;
  }

  /// Compares two mojom Uuid structures for equality. The auto-generated
  /// mojom bindings don't provide an equality operator.
  static bool equals(final mojom_uuid.Uuid uuid1, final mojom_uuid.Uuid uuid2) {
    return fromMojom(uuid1) == fromMojom(uuid2);
  }

  /// Computes a hash code for a mojom Uuid to allow it to be used in hash
  /// tables.
  static int getHashCode(final mojom_uuid.Uuid uuid) {
    return fromMojom(uuid).hashCode;
  }

  /// Convenience function for generating a random mojom Uuid.
  static mojom_uuid.Uuid randomMojom() {
    return toMojom(new Uuid.random());
  }

  /// Converts a base64 string to a mojom Uuid.
  static mojom_uuid.Uuid fromBase64(final String base64Uuid) {
    return UuidMojoHelpers.toMojom(Uuid.fromBase64(base64Uuid));
  }

  /// Converts a mojom Uuid to a base64 string.
  static String toBase64(final mojom_uuid.Uuid uuid) {
    return UuidMojoHelpers.fromMojom(uuid).toBase64();
  }
}

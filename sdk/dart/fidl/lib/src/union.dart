// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// ignore_for_file: public_member_api_docs

import 'hash_codes.dart';

abstract class Union {
  const Union();

  int get $ordinal;
  Object get $data;

  @override
  int get hashCode => deepHash([$ordinal, $data]);

  @override
  bool operator ==(Object other) {
    if (identical(this, other)) {
      return true;
    }
    if (runtimeType != other.runtimeType) {
      return false;
    }
    if (other is Union) {
      if ($ordinal != other.$ordinal) {
        return false;
      }

      return deepEquals($data, other.$data);
    }
    return false;
  }

  @override
  String toString() {
    return '$runtimeType(${$ordinal}: ${$data})';
  }
}

typedef UnionFactory<T> = T Function(int index, Object data);

// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// Copied from:
// https://raw.githubusercontent.com/google/quiver-dart/master/lib/src/core/hash.dart

/// Generates a hash code for multiple [objects].
int hashObjects(Iterable<Object> objects) =>
    _finish(objects.fold(0, (int h, Object i) => _combine(h, i.hashCode)));

/// Generates a hash code for two objects.
int hash2(Object a, Object b) =>
    _finish(_combine(_combine(0, a.hashCode), b.hashCode));

/// Generates a hash code for three objects.
int hash3(Object a, Object b, Object c) => _finish(
    _combine(_combine(_combine(0, a.hashCode), b.hashCode), c.hashCode));

/// Generates a hash code for four objects.
int hash4(Object a, Object b, Object c, Object d) => _finish(_combine(
    _combine(_combine(_combine(0, a.hashCode), b.hashCode), c.hashCode),
    d.hashCode));

// Jenkins hash functions

int _combine(int hash, int value) {
  hash = 0x1fffffff & (hash + value);
  hash = 0x1fffffff & (hash + ((0x0007ffff & hash) << 10));
  return hash ^ (hash >> 6);
}

int _finish(int hash) {
  hash = 0x1fffffff & (hash + ((0x03ffffff & hash) << 3));
  hash = hash ^ (hash >> 11);
  return 0x1fffffff & (hash + ((0x00003fff & hash) << 15));
}

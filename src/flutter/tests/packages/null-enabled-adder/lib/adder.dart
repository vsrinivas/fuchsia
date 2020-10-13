// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// Adds two integers treating null as zero
int safeAdd(int? x, int? y) => _add(x ?? 0, y ?? 0);

int _add(int x, int y) => x + y;

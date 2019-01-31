// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

void liba_symbol(void);
void module_symbol(void) {
    liba_symbol();
}

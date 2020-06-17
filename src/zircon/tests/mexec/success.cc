// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This test just succeeds unconditionally.
// The logic is that since this test is intended to test mexec, the fact that
// this test runs _at all_ is proof positive that the system was able to mexec.
int main() {
    return 0;
}

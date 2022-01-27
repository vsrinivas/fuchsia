// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub fn hostname_from_vec(hostnames: &Vec<String>) -> String {
    assert!(hostnames.len() > 0);
    let mut hostnames = hostnames.clone();
    hostnames.sort();
    hostnames[0].clone()
}

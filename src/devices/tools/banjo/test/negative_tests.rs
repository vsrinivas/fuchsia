// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![cfg(test)]

use super::negative_parse_test;

negative_parse_test!(bad_type, "banjo/bad_type.test.banjo");

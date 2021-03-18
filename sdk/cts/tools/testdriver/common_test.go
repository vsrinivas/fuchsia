// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package testdriver

import "flag"

var testDataDir = flag.String("test_data_dir", "", "Path to test data; only used in GN build")

// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package build

type ZBITest struct {
	// Path is the path to the test's file within the build directory.
	Path string `json:"path"`
}

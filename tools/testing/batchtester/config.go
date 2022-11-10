// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package batchtester

// Config contains all the information required for batchtester to run a batch
// of tests.
//
// All path fields are relative to the directory in which the batchtester is
// invoked.
type Config struct {
	// List of tests to run, in order.
	Tests []Test `json:"tests"`
}

type Test struct {
	// Human-readable test name.
	Name string `json:"name"`

	// Path to the test executable.
	Executable string `json:"executable"`

	// Directory in which the test should be run.
	Execroot string `json:"execroot"`
}

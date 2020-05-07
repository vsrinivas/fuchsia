// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package util

import "go.fuchsia.dev/fuchsia/tools/build/lib"

// UniqueName returns a globally unique name for this test. test.Name is
// available but is not necessarily a unique identifier for each test, so we use
// either path or package URL instead.
func UniqueName(test build.Test) string {
	if test.OS == "fuchsia" {
		return test.PackageURL
	}
	return test.Path
}

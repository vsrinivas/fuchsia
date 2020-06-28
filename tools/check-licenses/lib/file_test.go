// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package lib

import (
	"testing"
)

func TestFileIsSingleLicense(t *testing.T) {
	name := "LICENSE-THIRD-PARTY"
	singleLicenseFiles := []string{"LICENSE", "README"}
	if !isSingleLicenseFile(name, singleLicenseFiles) {
		t.Errorf("%v: %v is not a single license file", t.Name(), name)
	}
}

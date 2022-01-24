// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package pkg

import (
	"testing"
)

func TestValidateFailsWithNonZeroVersion(t *testing.T) {
	pkg := Package{
		Name:    "foo",
		Version: "1",
	}

	if err := pkg.Validate(); err != ErrInvalidPackageVersion {
		t.Fatalf("Validate should have failed with %s instead of %s", ErrInvalidPackageVersion, err)
	}
}

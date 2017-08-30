// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package translator

import (
	"testing"
)

func checkEq(t *testing.T, expected, actual interface{}) {
	if expected != actual {
		errorMsg := "Failed check: Expected (%q), Actual (%q)"
		if _, ok := expected.(string); !ok {
			errorMsg = "Failed check: Expected (%s), Actual (%s)"
		}
		t.Fatalf(errorMsg, expected, actual)
	}
}

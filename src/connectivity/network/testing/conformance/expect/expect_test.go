// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package expect

import (
	"os/exec"
	"testing"
)

// TestExpectExists tests whether the /usr/bin/expect binary is executable. Many network-conformance
// tests rely on expect, so this provides a useful signal as to why they might be broken.
func TestExpectExists(t *testing.T) {
	cmd := exec.Command(EXPECT_HOST_PATH, "-v")
	outputBytes, err := cmd.CombinedOutput()
	t.Logf(string(outputBytes))

	if err != nil {
		t.Errorf(
			"exec.Command(%q, \"-v\").CombinedOutput = %s, %s",
			EXPECT_HOST_PATH,
			string(outputBytes),
			err,
		)

		// If running it straight from /usr/bin/expect wasn't successful, try to see if it
		// exists on our PATH but is simply in a different location.
		cmd := exec.Command("command", "-v", "expect")
		outputBytes, err := cmd.CombinedOutput()
		t.Logf(string(outputBytes))

		if err != nil {
			t.Errorf(
				"exec.Command(%q, \"-v\", %q).CombinedOutput = %s, %s",
				"command",
				"expect",
				string(outputBytes),
				err,
			)
		}
	}
}

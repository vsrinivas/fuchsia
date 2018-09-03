// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"os/exec"
	"strings"
	"testing"
)

func Run(t *testing.T, cmd0 string, cmd ...string) string {
	out, err := exec.Command(cmd0, cmd...).CombinedOutput()
	if err != nil {
		t.Fatal(err)
	}
	return string(out)
}

func RunExpectingError(t *testing.T, cmd0 string, cmd ...string) {
	_, err := exec.Command(cmd0, cmd...).CombinedOutput()
	if err == nil {
		t.Errorf("expected `%s %s` to exit nonzero", cmd0, cmd)
	}
}

func RunQuietly(t *testing.T, cmd0 string, cmd ...string) {
	out := Run(t, cmd0, cmd...)
	if len(out) > 0 {
		t.Errorf("Expected '%s %s' to have no output but output was '%s'", cmd0, cmd, out)
	}
}

func TestOutput(t *testing.T) {
	t.Run("ifconfig route add", func(t *testing.T) {
		before := Run(t, "/system/bin/ifconfig", "route", "show")
		RunExpectingError(t, "/system/bin/ifconfig", "route", "add", "1.2.3.4/14", "gateway", "9.8.7.6", "iface", "NON_EXISTENT_INTERFACE_FORCES_FAILURE")
		after := Run(t, "/system/bin/ifconfig", "route", "show")

		if before != after {
			t.Errorf("Expected ifconfig route add failure but the routes changed: '%s' vs '%s'", before, after)
		}
	})

	t.Run("ifconfig route del", func(t *testing.T) {
		// Add a route.
		RunQuietly(t, "/system/bin/ifconfig", "route", "add", "1.2.3.4/14", "gateway", "9.8.7.6", "iface", "lo")

		out := Run(t, "/system/bin/ifconfig", "route", "show")
		expected := "1.2.3.4/14 via 9.8.7.6 lo"
		if !strings.Contains(out, expected) {
			t.Errorf("ifconfig route add failed, couldn't find '%s' in '%s'", expected, out)
		}

		// Try to delete it but the gateway mismatches so it should fail.
		RunExpectingError(t, "/system/bin/ifconfig", "route", "del", "1.2.3.4/14", "gateway", "9.9.9.9")
		out = Run(t, "/system/bin/ifconfig", "route", "show")
		if !strings.Contains(out, expected) {
			t.Errorf("ifconfig route del removed '%s' from '%s' but it should not have", expected, out)
		}

		// Try to delete it but the mask length mismatches so it should fail.
		RunExpectingError(t, "/system/bin/ifconfig", "route", "del", "1.2.3.4/15")
		out = Run(t, "/system/bin/ifconfig", "route", "show")
		if !strings.Contains(out, expected) {
			t.Errorf("ifconfig route del removed '%s' from '%s' but it should not have", expected, out)
		}

		// Now delete it.
		RunQuietly(t, "/system/bin/ifconfig", "route", "del", "1.2.3.4/14", "gateway", "9.8.7.6")
		out = Run(t, "/system/bin/ifconfig", "route", "show")
		if strings.Contains(out, expected) {
			t.Errorf("ifconfig route del failed, did not expect to find '%s' in '%s'", expected, out)
		}

		// Try to delete it again, it should fail.
		RunExpectingError(t, "/system/bin/ifconfig", "route", "del", "1.2.3.4/14")
		out = Run(t, "/system/bin/ifconfig", "route", "show")
		if strings.Contains(out, expected) {
			t.Errorf("ifconfig route del failed, did not expect to find '%s' in '%s'", expected, out)
		}
	})

	t.Run("ifconfig route parse error formatting", func(t *testing.T) {
		out, err := exec.Command("/system/bin/ifconfig", "route", "add").CombinedOutput()
		if err == nil {
			t.Errorf("ifconfig route add returned success without enough arguments. output: \n%s", out)
		}
		expected := "Not enough arguments"
		if !strings.Contains(string(out), expected) {
			t.Errorf("want `ifconfig route add` to print \"%s\", got \"%s\"", expected, out)
		}

		out, err = exec.Command("/system/bin/ifconfig", "route", "add", "1.2.3.4/14").CombinedOutput()
		if err == nil {
			t.Errorf("ifconfig route add returned success with neither gateway or iface specified. output: \n%s", out)
		}
		expected = "Error adding route"
		if !strings.Contains(string(out), expected) {
			t.Errorf("want `ifconfig route add` to print \"%s\", got \"%s\"", expected, out)
		}
	})
}

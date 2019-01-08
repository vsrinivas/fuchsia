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
	t.Helper()
	out := Run(t, cmd0, cmd...)
	if len(out) > 0 {
		t.Errorf("Expected '%s %s' to have no output but output was '%s'", cmd0, cmd, out)
	}
}

func TestOutput(t *testing.T) {
	t.Run("ifconfig route add", func(t *testing.T) {
		before := Run(t, "/bin/ifconfig", "route", "show")
		RunExpectingError(t, "/bin/ifconfig", "route", "add", "1.2.3.4/14", "gateway", "9.8.7.6", "iface", "NON_EXISTENT_INTERFACE_FORCES_FAILURE")
		after := Run(t, "/bin/ifconfig", "route", "show")

		if before != after {
			t.Errorf("Expected ifconfig route add failure but the routes changed: '%s' vs '%s'", before, after)
		}
	})

	t.Run("ifconfig route del", func(t *testing.T) {
		// Add a route.
		RunQuietly(t, "/bin/ifconfig", "route", "add", "1.2.3.4/14", "gateway", "9.8.7.6", "iface", "lo")

		out := Run(t, "/bin/ifconfig", "route", "show")
		expected := "1.2.3.4/14 via 9.8.7.6 lo"
		if !strings.Contains(out, expected) {
			t.Errorf("ifconfig route add failed, couldn't find '%s' in '%s'", expected, out)
		}

		// Try to delete it but the gateway mismatches so it should fail.
		RunExpectingError(t, "/bin/ifconfig", "route", "del", "1.2.3.4/14", "gateway", "9.9.9.9")
		out = Run(t, "/bin/ifconfig", "route", "show")
		if !strings.Contains(out, expected) {
			t.Errorf("ifconfig route del removed '%s' from '%s' but it should not have", expected, out)
		}

		// Try to delete it but the mask length mismatches so it should fail.
		RunExpectingError(t, "/bin/ifconfig", "route", "del", "1.2.3.4/15")
		out = Run(t, "/bin/ifconfig", "route", "show")
		if !strings.Contains(out, expected) {
			t.Errorf("ifconfig route del removed '%s' from '%s' but it should not have", expected, out)
		}

		// Now delete it.
		RunQuietly(t, "/bin/ifconfig", "route", "del", "1.2.3.4/14", "gateway", "9.8.7.6")
		out = Run(t, "/bin/ifconfig", "route", "show")
		if strings.Contains(out, expected) {
			t.Errorf("ifconfig route del failed, did not expect to find '%s' in '%s'", expected, out)
		}

		// Try to delete it again, it should fail.
		RunExpectingError(t, "/bin/ifconfig", "route", "del", "1.2.3.4/14")
		out = Run(t, "/bin/ifconfig", "route", "show")
		if strings.Contains(out, expected) {
			t.Errorf("ifconfig route del failed, did not expect to find '%s' in '%s'", expected, out)
		}
	})

	t.Run("ifconfig route parse error formatting", func(t *testing.T) {
		out, err := exec.Command("/bin/ifconfig", "route", "add").CombinedOutput()
		if err == nil {
			t.Errorf("ifconfig route add returned success without enough arguments. output: \n%s", out)
		}
		expected := "Not enough arguments"
		if !strings.Contains(string(out), expected) {
			t.Errorf("want `ifconfig route add` to print \"%s\", got \"%s\"", expected, out)
		}

		out, err = exec.Command("/bin/ifconfig", "route", "add", "1.2.3.4/14").CombinedOutput()
		if err == nil {
			t.Errorf("ifconfig route add returned success with neither gateway or iface specified. output: \n%s", out)
		}
		expected = "Error adding route to route table"
		if !strings.Contains(string(out), expected) {
			t.Errorf("want `ifconfig route add` to print \"%s\", got \"%s\"", expected, out)
		}
	})

	t.Run("Interface name exact match: `ifconfig lo` should return 1 interface", func(t *testing.T) {
		out, err := exec.Command("/bin/ifconfig", "lo").CombinedOutput()
		if err != nil {
			t.Errorf("want no error but got error:\n%s", out)
		}
		headlines := findLinesContainingHWaddr(string(out))
		if len(headlines) != 1 {
			t.Errorf("want exactly 1 interface from `ifconfig lo`, got: %d", len(headlines))
		} else {
			expected := "lo\tHWaddr  Id:1"
			if !strings.HasPrefix(headlines[0], expected) {
				t.Errorf("want interface `lo` from `ifconfig lo`, got:\n%s", out)
			}
		}
	})

	t.Run("Interface name no match: `ifconfig o` should return 0 interface", func(t *testing.T) {
		out, err := exec.Command("/bin/ifconfig", "o").CombinedOutput()
		if err != nil {
			t.Errorf("want no error but got error:\n%s", out)
		}
		expected := "ifconfig: no such interface"
		if !(strings.HasPrefix(string(out), expected)) {
			t.Errorf("want no interface from `ifconfig o`, got:\n%s", out)
		}
	})

	t.Run("Interface name partial match: `ifconfig l` should return 1 interface", func(t *testing.T) {
		out, err := exec.Command("/bin/ifconfig", "l").CombinedOutput()
		if err != nil {
			t.Errorf("want no error from `ifconfig l` but got error:\n%s", out)
		}
		headlines := findLinesContainingHWaddr(string(out))
		if len(headlines) != 1 {
			t.Errorf("want exactly 1 interface from `ifconfig l`, got: %d", len(headlines))
		} else {
			expected := "lo\tHWaddr  Id:1"
			if !strings.HasPrefix(headlines[0], expected) {
				t.Errorf("want interface `lo` from `ifconfig l`, got:\n%s", out)
			}
		}
	})

	t.Run("ambiguous match: `ifconfig \"\"` should return 0 interfaces", func(t *testing.T) {
		// TODO(NET-1749): add an interface to ensure there are at least 2 interfaces.
	})
}

func findLinesContainingHWaddr(in string) []string {
	var out []string
	for _, line := range strings.Split(in, "\n") {
		if strings.Contains(line, "HWaddr") {
			out = append(out, line)
		}
	}
	return out
}

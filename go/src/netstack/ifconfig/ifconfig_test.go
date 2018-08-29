// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"os/exec"
	"strings"
	"testing"
)

func TestOutput(t *testing.T) {
	t.Run("ifconfig route", func(t *testing.T) {
		err := exec.Command("/system/bin/ifconfig", "route", "add", "1.2.3.4/14", "gateway", "9.8.7.6", "iface", "lo").Run()
		if err != nil {
			t.Fatal(err)
		}
		out, err := exec.Command("/system/bin/ifconfig", "route", "show").CombinedOutput()
		expected := "1.2.3.4/14 via 9.8.7.6 lo"
		if !strings.Contains(string(out), expected) {
			t.Errorf("ifconfig route add failed, couldn't find '%s' in '%s'", expected, out)
		}
	})
}

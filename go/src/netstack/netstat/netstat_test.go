// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"os/exec"
	"strings"
	"testing"
)

func TestICMPOutputPresence(t *testing.T) {
	cmd := exec.Command("/system/bin/netstat", "-s")
	raw, err := cmd.CombinedOutput()
	if err != nil {
		t.Errorf("%v\n", err)
	}

	out := string(raw)
	metrics := []string{"ICMP messages received", "input ICMP message failed.", "ICMP messages sent", "ICMP messages failed", "ICMP input histogram", "ICMP output histogram"}

	for _, m := range metrics {
		if !strings.Contains(out, m) {
			t.Errorf("ICMP metric \"%s\" not present", m)
		}
	}
}

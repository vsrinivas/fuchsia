// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"os/exec"
	"regexp"
	"strconv"
	"strings"
	"testing"
)

func TestICMPOutputPresence(t *testing.T) {
	out := output(t, "/system/bin/netstat", "-s")
	metrics := []string{"ICMP messages received", "input ICMP message failed.", "ICMP messages sent", "ICMP messages failed", "ICMP input histogram", "ICMP output histogram"}

	for _, m := range metrics {
		if !strings.Contains(out, m) {
			t.Errorf("ICMP metric \"%s\" not present", m)
		}
	}
}

func TestICMPSentCount(t *testing.T) {
	reqs := parseSentCount(t, output(t, "/system/bin/netstat", "-s"))

	ping := exec.Command("/boot/bin/ping", "-c", "1", "localhost")
	err := ping.Run()
	switch err.(type) {
	case *exec.Error:
		t.Fatalf("Failed to run ping: %v", err)
	case *exec.ExitError:
		// fallthrough, ping doesn't have to successfully resolve the host for sent count to increase
	}

	reqsAfter := parseSentCount(t, output(t, "/system/bin/netstat", "-s"))

	if !(reqsAfter > reqs) {
		t.Errorf("echo request count did not increase after running ping: before %d, after %d", reqs, reqsAfter)
	}
}

func output(t *testing.T, command string, args ...string) string {
	cmd := exec.Command(command, args...)
	out, err := cmd.CombinedOutput()
	if err != nil {
		t.Errorf("Couldn't execute %s: %v\n", command, err)
		if out != nil {
			t.Errorf("Combined output:\n%s", out)
		}
		t.Fail()
	}

	return string(out)
}

var sentCountMatcher *regexp.Regexp = regexp.MustCompile("([0-9]+) ICMP messages sent")

func parseSentCount(t *testing.T, nsOut string) int64 {
	matches := sentCountMatcher.FindStringSubmatch(nsOut)
	if len(matches) == 0 {
		t.Fatalf("Did not match %+v in %s", sentCountMatcher, nsOut)
	}

	count, err := strconv.ParseInt(matches[1], 10, 64)
	if err != nil {
		t.Errorf("Could not parse echo request count from string: %s", matches[0])
	}

	return count
}

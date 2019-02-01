// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"context"
	"fmt"
	"os/exec"
	"regexp"
	"strconv"
	"strings"
	"testing"
)

var sentCountMatcher *regexp.Regexp = regexp.MustCompile("([0-9]+) ICMP messages sent")

func parseSentCount(outStr string) (int, error) {
	matches := sentCountMatcher.FindStringSubmatch(outStr)
	if expected, got := 2, len(matches); got != expected {
		return 0, fmt.Errorf("expected %d matches of %+v in\n%s\n, got %d", expected, sentCountMatcher, outStr, got)
	}

	return strconv.Atoi(matches[1])
}

func TestOutput(t *testing.T) {
	out, err := exec.Command("/bin/netstat", "-s").CombinedOutput()
	if err != nil {
		t.Fatal(err)
	}
	outStr := string(out)

	t.Run("ICMP", func(t *testing.T) {
		for _, metric := range []string{
			"ICMP messages received",
			"input ICMP message failed.",
			"ICMP messages sent",
			"ICMP messages failed",
			"ICMP input histogram",
			"ICMP output histogram",
		} {
			if !strings.Contains(outStr, metric) {
				t.Errorf("ICMP metric %q not present", metric)
			}
		}
	})

	t.Run("IP", func(t *testing.T) {
		for _, metric := range []string{
			"total packets received",
			"with invalid addresses",
			"incoming packets delivered",
			"requests sent out",
			"outgoing packets with errors",
		} {
			if !strings.Contains(outStr, metric) {
				t.Errorf("IP metric %q not present", metric)
			}
		}
	})

	t.Run("UDP", func(t *testing.T) {
		for _, metric := range []string{
			"packets received",
			"packet receive errors",
			"packets to unknown ports received",
			"receive buffer errors",
			"malformed packets received",
			"packets sent",
		} {
			if !strings.Contains(outStr, metric) {
				t.Errorf("IP metric %q not present", metric)
			}
		}
	})

	if t.Failed() {
		t.Logf("out: \n%s", outStr)
		t.FailNow()
	}

	t.Run("ICMPSentCount", func(t *testing.T) {
		reqsBefore, err := parseSentCount(outStr)
		if err != nil {
			t.Fatal(err)
		}

		ctx, cancel := context.WithCancel(context.Background())
		defer cancel()

		ping := exec.CommandContext(ctx, "/boot/bin/ping", "-c", "1", "localhost")
		if err := ping.Run(); err != nil {
			t.Fatalf("ping failed: %+v", err)
		}

		out, err := exec.Command("/bin/netstat", "-s").CombinedOutput()
		if err != nil {
			t.Fatal(err)
		}
		outStr := string(out)
		reqsAfter, err := parseSentCount(outStr)
		if err != nil {
			t.Fatal(err)
		}

		if !(reqsAfter > reqsBefore) {
			t.Errorf("echo request count did not increase after running ping: before %d, after %d", reqsBefore, reqsAfter)
		}
	})
}

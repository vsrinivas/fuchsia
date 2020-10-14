// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package symbolize

import (
	"context"
	"io"
	"strings"
	"testing"
	"time"

	"github.com/google/go-cmp/cmp"
)

func TestStartParsing(t *testing.T) {
	ctx := context.Background()

	// Receives from a channel and returns the received element, or fails the
	// test if the timeout is exceeded.
	receiveWithTimeout := func(t *testing.T, c <-chan InputLine) InputLine {
		t.Helper()
		timeout := time.Second
		select {
		case line := <-c:
			return line
		case <-time.After(timeout):
			t.Fatalf("channel receive timed out after %s", timeout)
		}
		return InputLine{}
	}

	// Starts the log parser in the background, and returns two channels:
	// - A write-only channel via which mock logs can be sent to the parser.
	// - A read-only channel on which the parser will send the parsed logs.
	setup := func(ctx context.Context, t *testing.T) (chan<- string, <-chan InputLine) {
		logReader, logWriter := io.Pipe()
		parsedLines := StartParsing(ctx, logReader)
		ctx, cancel := context.WithCancel(ctx)
		t.Cleanup(cancel)

		mockLogs := make(chan string, 1)

		go func() {
			for {
				select {
				case s := <-mockLogs:
					// Log lines must end in newlines to delineate log lines for the parser.
					if !strings.HasSuffix(s, "\n") {
						s += "\n"
					}
					logWriter.Write([]byte(s))
				case <-ctx.Done():
					return
				}
			}
		}()
		return mockLogs, parsedLines
	}

	cmpOpts := cmp.Options{
		cmp.AllowUnexported(InputLine{}, LogLine{}, logHeader{}, sysLogHeader{}),
	}

	t.Run("handles kernel logs", func(t *testing.T) {
		mockLogs, parsedLines := setup(ctx, t)
		mockLogs <- "[168.122] 123.456> minfs: shutting down"
		expected := InputLine{
			LogLine: LogLine{
				lineno: 1,
				header: logHeader{
					time:    168.122,
					process: 123,
					thread:  456,
				},
				source: Process(123),
			},
			msg: " minfs: shutting down",
		}
		line := receiveWithTimeout(t, parsedLines)
		if diff := cmp.Diff(expected, line, cmpOpts...); diff != "" {
			t.Errorf("line parser mismatch (-want +got):\n%s", diff)
		}
	})

	t.Run("handles syslog with uptime", func(t *testing.T) {
		mockLogs, parsedLines := setup(ctx, t)
		mockLogs <- "[00016.680][123][456][cobalt, fidl_service] INFO: Clock has been initialized"
		expected := InputLine{
			LogLine: LogLine{
				lineno: 1,
				header: sysLogHeader{
					uptime:  16.68,
					process: 123,
					thread:  456,
					tags:    "cobalt, fidl_service",
					typ:     "INFO",
				},
				source: Process(123),
			},
			msg: " Clock has been initialized",
		}
		line := receiveWithTimeout(t, parsedLines)
		if diff := cmp.Diff(expected, line, cmpOpts...); diff != "" {
			t.Errorf("line parser mismatch (-want +got):\n%s", diff)
		}
	})

	t.Run("handles syslog with UTC time", func(t *testing.T) {
		mockLogs, parsedLines := setup(ctx, t)
		mockLogs <- "[2020-10-02 14:15:01][123][456][cobalt, fidl_service] INFO: Clock has been initialized"
		expected := InputLine{
			LogLine: LogLine{
				lineno: 1,
				header: sysLogHeader{
					datetime: "2020-10-02 14:15:01",
					process:  123,
					thread:   456,
					tags:     "cobalt, fidl_service",
					typ:      "INFO",
				},
				source: Process(123),
			},
			msg: " Clock has been initialized",
		}
		line := receiveWithTimeout(t, parsedLines)
		if diff := cmp.Diff(expected, line, cmpOpts...); diff != "" {
			t.Errorf("line parser mismatch (-want +got):\n%s", diff)
		}
	})
}

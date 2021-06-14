// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package e2e

import (
	"bytes"
	"log"
	"os"
	"os/exec"
	"strings"
	"testing"
)

// TestRetry tests launchEmuWithRetry by captureing log output from stdout.
func TestRetry(t *testing.T) {
	testCases := []struct {
		name          string
		command       string
		attempts      int
		expectedRetry int
		expectError   bool
	}{
		{
			name:          "fail_with_retry1",
			command:       "abcd", // This is an invalid command, should result in exit code 127.
			attempts:      5,
			expectedRetry: 4,
			expectError:   true,
		}, {
			name:          "fail_with_retry2",
			command:       "efgh", // This is an invalid command, should result in exit code 127.
			attempts:      10,
			expectedRetry: 9,
			expectError:   true,
		}, {
			name:          "fail_no_retry",
			command:       "false", // This will produce exit code 1
			attempts:      10,
			expectedRetry: 0,
			expectError:   true,
		}, {
			name:          "pass",
			command:       "ls", // This will produce exit code 0
			attempts:      50,
			expectedRetry: 0,
			expectError:   false,
		},
	}
	defer log.SetOutput(os.Stderr)
	for _, tc := range testCases {
		t.Run(tc.name, func(t *testing.T) {
			cmd := exec.Command(tc.command)
			var buf bytes.Buffer
			log.SetOutput(&buf)
			err := launchEmuWithRetry(tc.attempts, cmd)
			log.SetOutput(os.Stderr)
			if tc.expectError && err != nil {
				out := strings.Split(buf.String(), "\n")
				retry_cnt := 0
				for _, line := range out {
					if strings.Contains(line, "Retry launching emulator") {
						retry_cnt++
					}
				}
				if retry_cnt != tc.expectedRetry {
					t.Errorf("incorrect number of retries expect %d retries, got %d", tc.expectedRetry, retry_cnt)
				}
			} else if tc.expectError && err == nil {
				t.Error("expect error to be thrown, got nil.")
			} else if !tc.expectError && err != nil {
				t.Errorf("did not expect error to be thrown, got error %s.", err)
			}
		})
	}
}

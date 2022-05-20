// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package emulator

import (
	"bufio"
	"bytes"
	"fmt"
	"os/exec"
	"strings"
	"testing"
)

func TestCheckForLogMessage(t *testing.T) {
	logLines := []string{"Some message", "Another message", "First message we're looking for", "Another message", "Second message we're looking for"}
	testStr := strings.Join(logLines, "\n")
	// attach a newline, because the reader expects this
	testStr = fmt.Sprintf("%s\n", testStr)
	fakeReader := bufio.NewReader(strings.NewReader(testStr))
	i := Instance{}
	if err := i.checkForLogMessage(fakeReader, logLines[2]); err != nil {
		t.Fatal(err)
	}

	fakeReader = bufio.NewReader(strings.NewReader(testStr))

	i = Instance{stdout: fakeReader}
	if err := i.WaitForLogMessage(logLines[2]); err != nil {
		t.Fatal(err)
	}

	fakeReader = bufio.NewReader(strings.NewReader(testStr))

	i = Instance{stdout: fakeReader}
	if err := i.WaitForLogMessages([]string{logLines[2]}); err != nil {
		t.Fatal(err)
	}

	fakeReader = bufio.NewReader(strings.NewReader(testStr))

	i = Instance{stdout: fakeReader}
	if err := i.WaitForLogMessages([]string{logLines[2], logLines[4]}); err != nil {
		t.Fatal(err)
	}

	fakeReader = bufio.NewReader(strings.NewReader(testStr))

	i = Instance{stdout: fakeReader}
	if err := i.WaitForLogMessages([]string{logLines[4], logLines[2]}); err != nil {
		t.Fatal(err)
	}
}

func TestSetLogDestination(t *testing.T) {
	testStr := `line 1
line 2
line 3
`
	// Creating a no-op exec.Cmd so that i.Wait() can wait for it to complete
	cmd := exec.Command("true")
	if err := cmd.Start(); err != nil {
		t.Fatal(err)
	}

	i := Instance{
		stdout: bufio.NewReader(strings.NewReader(testStr)),
		cmd:    cmd,
	}

	var buf bytes.Buffer
	i.SetLogDestination(&buf)

	if _, err := i.Wait(); err != nil {
		t.Fatal(err)
	}
	if buf.String() != testStr {
		t.Fatalf("got buf.String() = %q, wanted %q", buf.String(), testStr)
	}
}

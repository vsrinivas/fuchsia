// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package lib

import (
	"bufio"
	"fmt"
	"io"
	"io/ioutil"
	"strings"
	"testing"
)

const invalidPath = "/invalid/path"

func getTempdir(t *testing.T) string {
	tmpDir, err := ioutil.TempDir("", "clusterfuchsia_test")

	if err != nil {
		t.Fatalf("error creating temp dir: %s", err)
	}

	return tmpDir
}

// Must be cleaned up by caller
func createTempfileWithContents(t *testing.T, contents string, extension string) string {
	file, err := ioutil.TempFile("", "*."+extension)
	if err != nil {
		t.Fatalf("error creating tempfile: %s", err)
	}

	file.WriteString(contents)
	file.Close()

	return file.Name()
}

// This is used by both the mockBuild and the mock symbolize subprocess
func fakeSymbolize(in io.Reader, out io.Writer) error {
	scanner := bufio.NewScanner(in)
	for scanner.Scan() {
		line := scanner.Text()
		io.WriteString(out, strings.ReplaceAll(line, "{{{0x41}}}", "wow.c:1")+"\n")
	}
	if err := scanner.Err(); err != nil {
		return fmt.Errorf("failed during scan: %s", err)
	}
	return nil
}

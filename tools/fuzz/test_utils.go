// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package fuzz

import (
	"bufio"
	"crypto/sha256"
	"fmt"
	"io"
	"io/ioutil"
	"math/rand"
	"os"
	"strings"
	"testing"
)

const invalidPath = "/invalid/path"

func expectPathAbsent(t *testing.T, path string) {
	if _, err := os.Stat(path); !os.IsNotExist(err) {
		t.Fatalf("expected path %q to not exist, but it does", path)
	}
}

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
func fakeSymbolize(in io.ReadCloser, out io.Writer) error {
	defer in.Close()

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

func getFileHash(t *testing.T, path string) []byte {
	f, err := os.Open(path)
	if err != nil {
		t.Fatalf("error opening file: %s", err)
	}
	defer f.Close()

	hash := sha256.New()
	if _, err := io.Copy(hash, f); err != nil {
		t.Fatalf("error while hashing file %q: %s", path, err)
	}

	return hash.Sum(nil)
}

// Create a file, with random contents to allow for simple change detection
// Note: CPRNG seeds need to be propagated to subprocesses to avoid collisions
func touchRandomFile(t *testing.T, path string) {
	contents := make([]byte, 16)
	if _, err := rand.Read(contents); err != nil {
		t.Fatalf("Error getting random contents: %s", err)
	}
	if err := ioutil.WriteFile(path, contents, 0o600); err != nil {
		t.Fatalf("Error touching file %q: %s", path, err)
	}
}

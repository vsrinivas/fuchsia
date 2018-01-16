// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"fmt"
	"io/ioutil"
	"os"
	"testing"

	"github.com/google/go-cmp/cmp"
)

const lineFormat string = "%s=%s\n"

func TestReadManifestWithEmptyLine(t *testing.T) {
	r, err := ioutil.TempFile("", "read-manifest-test")
	if err != nil {
		t.Fatalf("failed creating temp file: %s", err)
	}
	defer os.Remove(r.Name())

	content := entryMap()

	count := 0
	for k, v := range content {
		if count == 1 {
			if _, err = r.WriteString("\n"); err != nil {
				t.Fatalf("Write error inserting blank line")
			}
		}

		if _, err := fmt.Fprintf(r, lineFormat, k, v); err != nil {
			t.Fatalf("Writing line failed: %s", err)
		}

		count += 1
	}

	r.Close()

	entries, err := readManifest(r.Name())
	if err != nil {
		t.Fatalf("manifest read failed: %s", err)
	}

	verifyEntries(entries, content, t)
}

func TestReadManifest(t *testing.T) {
	r, err := ioutil.TempFile("", "read-manifest-test")
	if err != nil {
		t.Fatalf("failed creating temp file: %s", err)
	}
	defer os.Remove(r.Name())

	content := entryMap()
	writeManifest(r, content, t)
	r.Close()

	entries, err := readManifest(r.Name())
	if err != nil {
		t.Fatalf("manifest read failed: %s", err)
	}

	verifyEntries(entries, content, t)
}

func TestReadEmptyManifest(t *testing.T) {
	r, err := ioutil.TempFile("", "read-manifest-test")
	if err != nil {
		t.Fatalf("failed creating temp file: %s", err)
	}
	defer os.Remove(r.Name())

	r.Close()

	entries, err := readManifest(r.Name())
	if err != nil {
		t.Fatalf("manifest read failed: %s", err)
	}

	if len(entries) != 0 {
		t.Error("Parsing of empty file produced manifest entries.")
		for _, v := range entries {
			t.Errorf("remote path: %q, local path: %q\n", v.remotePath, v.localPath)
		}
		t.Fail()
	}
}

func entryMap() map[string]string {
	content := make(map[string]string)
	content["foo"] = "bar"
	content["remote/path/to/file1"] = "local/path/to/file1"
	content["dangling"] = ""
	content["last"] = "final/entry"
	return content
}

// writeManifest writes the given map to a file, inserting an equals sign
// between key and value and adding a new line. The func 'cb' which is passed
// will be called after each line is written and be handled the 0-based line
// index that was just written and a reference to the output file.
func writeManifest(f *os.File, lines map[string]string, t *testing.T) {
	for k, v := range lines {
		if _, err := fmt.Fprintf(f, lineFormat, k, v); err != nil {
			t.Fatalf("Writing line failed: %s", err)
		}
	}
}

func entryListToMap(entries []manifestEntry) map[string]string {
	m := make(map[string]string, len(entries))
	for _, entry := range entries {
		m[entry.remotePath] = entry.localPath
	}
	return m
}

// verifyEntries checks that the items in the entries list appear in the map
// passed in and that both the list and the map contain the same number of
// items.
func verifyEntries(entries []manifestEntry, orig map[string]string,
	t *testing.T) {
	if len(entries) != len(orig) {
		t.Errorf("%s\n", cmp.Diff(orig, entryListToMap(entries), []cmp.Option{}...))
		t.Fatalf("Unexpected line count, expected %d, found %d", len(entries),
			len(orig))
	}

	for _, entry := range entries {
		val, ok := orig[entry.remotePath]
		if !ok {
			t.Fatalf("Unexpected content in parsed file, %q", entry.remotePath)
		}

		if val != entry.localPath {
			t.Fatalf("Local path values do not match, found: %q, expected %q",
				entry.localPath, val)
		}

		delete(orig, entry.remotePath)
	}

	if len(orig) != 0 {
		t.Errorf("Some entires not found in manifest file")
		for k, _ := range orig {
			t.Errorf("Entry %q remains\n", k)
		}
		t.Fail()
	}
}

// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package source

import (
	"bytes"
	"encoding/json"
	"io/ioutil"
	"os"
	"path/filepath"
	"testing"
)

func TestFileStore(t *testing.T) {
	tmpdir, err := ioutil.TempDir("", "amber-filestore-test")
	if err != nil {
		t.Fatal(err)
	}
	defer os.RemoveAll(tmpdir)

	// check that NewFileStore creates it's parent directories if necessary
	os.RemoveAll(tmpdir)
	s, err := NewFileStore(filepath.Join(tmpdir, "amber-store"))
	if err != nil {
		t.Fatal(err)
	}
	_, err = os.Stat(tmpdir)
	if err != nil {
		t.Fatal(err)
	}

	// check that NewFileStore works with a pre-existing dir
	os.RemoveAll(tmpdir)
	err = os.Mkdir(tmpdir, 0755)
	if err != nil {
		t.Fatal(err)
	}
	s, err = NewFileStore(filepath.Join(tmpdir, "amber-store"))
	if err != nil {
		t.Fatal(err)
	}

	exampleMeta := map[string]json.RawMessage{
		"roots.json":   json.RawMessage(`{"some":"keys"}`),
		"targets.json": json.RawMessage(`{"some":"targets"}`),
	}

	// GetMeta should not error even if the store was just created. The returned
	// metadata should simply be empty.
	m, err := s.GetMeta()
	if err != nil {
		t.Fatal(err)
	}
	if got, want := len(m), 0; got != want {
		t.Fatalf("got %d, want %d", got, want)
	}

	// Check that we can store and retrieve a value:
	if err := s.SetMeta("roots.json", exampleMeta["roots.json"]); err != nil {
		t.Fatal(err)
	}
	m, err = s.GetMeta()
	if err != nil {
		t.Fatal(err)
	}
	if got, want := len(m), 1; got != want {
		t.Fatalf("got %d, want %d", got, want)
	}
	if got, want := m["roots.json"], exampleMeta["roots.json"]; !bytes.Equal(got, want) {
		t.Fatalf("SetMeta&GetMeta: got %#v, want %#v", got, want)
	}

	// Check that new values are appended to the existing data, not overwritten:
	if err := s.SetMeta("targets.json", exampleMeta["targets.json"]); err != nil {
		t.Fatal(err)
	}
	m, err = s.GetMeta()
	if err != nil {
		t.Fatal(err)
	}
	if got, want := len(m), 2; got != want {
		t.Fatalf("got %d, want %d", got, want)
	}
	for k, v := range exampleMeta {
		if got, want := json.RawMessage(m[k]), json.RawMessage(v); !bytes.Equal(got, want) {
			t.Fatalf("SetMeta&GetMeta: got %#v, want %#v", got, want)
		}
	}
}

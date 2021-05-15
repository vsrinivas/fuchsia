// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package fuzz

import (
	"os"
	"path/filepath"
	"testing"

	"github.com/google/go-cmp/cmp"
)

func TestHandleSerialize(t *testing.T) {
	connector := &SSHConnector{Port: 1234}
	launcher := NewQemuLauncher(nil)
	launcher.TmpDir = "フクシャ"

	data := HandleData{connector, launcher}
	handle, err := NewHandleWithData(data)
	if err != nil {
		t.Fatalf("error creating handle: %s", err)
	}
	defer handle.Release()

	s := handle.Serialize()
	reloadedHandle, err := LoadHandleFromString(s)
	if err != nil {
		t.Fatalf("error deserializing handle: %s", err)
	}

	result, err := reloadedHandle.GetData()
	if err != nil {
		t.Fatalf("error getting handle data: %s", err)
	}

	if diff := cmp.Diff(&data, result, cmp.AllowUnexported(HandleData{},
		SSHConnector{}, QemuLauncher{})); diff != "" {
		t.Fatalf("incorrect reloaded handle (-want +got):\n%s", diff)
	}
}

func TestHandleRelease(t *testing.T) {
	handle, err := NewHandleWithData(HandleData{})
	if err != nil {
		t.Fatalf("error creating handle: %s", err)
	}
	s := handle.Serialize()

	handle.Release()

	if err := handle.SetData(HandleData{}); err == nil {
		t.Fatalf("SetData unexpectedly succeeded on released handle %q", handle)
	}

	if _, err := LoadHandleFromString(s); err == nil {
		t.Fatalf("LoadHandleFromString unexpectedly succeeded on released handle %q", handle)
	}

	if _, err := handle.GetData(); err == nil {
		t.Fatalf("GetData unexpectedly succeeded on released handle %q", handle)
	}
}

func TestLoadInvalidHandle(t *testing.T) {
	// Point to nonexistent file
	invalidPath := filepath.Join(t.TempDir(), "invalid")
	if _, err := LoadHandleFromString(invalidPath); err == nil {
		t.Fatalf("expected error loading from invalid path, but succeeded")
	}

	// Point to file that exists, but does not contain JSON
	nonJsonFile := createTempfileWithContents(t, "garbage", "json")
	defer os.Remove(nonJsonFile)
	if _, err := LoadHandleFromString(nonJsonFile); err == nil {
		t.Fatalf("expected error loading from non-json file, but succeeded")
	}
}

func TestHandleGetEmptyData(t *testing.T) {
	// JSON with empty contents
	emptyJsonFile := createTempfileWithContents(t, `{}`, "json")
	defer os.Remove(emptyJsonFile)
	handle, err := LoadHandleFromString(emptyJsonFile)
	if err != nil {
		t.Fatalf("error deserializing handle: %s", err)
	}
	if _, err := handle.GetData(); err != nil {
		t.Fatalf("error loading from empty handle: %s", err)
	}
}

func TestHandleGetInvalidData(t *testing.T) {
	// JSON with unsupported type
	badTypeFile := createTempfileWithContents(t, `{"ConnectorType": "UnsupportedConnector"}`, "json")
	defer os.Remove(badTypeFile)
	handle, err := LoadHandleFromString(badTypeFile)
	if err != nil {
		t.Fatalf("error deserializing handle: %s", err)
	}
	if data, err := handle.GetData(); err == nil {
		t.Fatalf("expected error loading invalid type, but succeeded: %+v", data)
	}
}

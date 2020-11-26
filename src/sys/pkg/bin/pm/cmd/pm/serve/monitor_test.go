// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package serve

import (
	"fmt"
	"io/ioutil"
	"os"
	"path/filepath"
	"testing"
	"time"

	"go.fuchsia.dev/fuchsia/src/sys/pkg/bin/pm/fswatch"
)

func makeTestMetadata(version int) []byte {
	template := `{
		"signed": {
			"version": %v
		}
	}`
	return []byte(fmt.Sprintf(template, version))
}

func TestMetadataMissingShutdown(t *testing.T) {
	dir := t.TempDir()
	w, err := fswatch.NewWatcher()
	if err != nil {
		t.Fatal(err)
	}
	defer w.Close()

	monitor := NewMetadataMonitor(filepath.Join(dir, "missing.json"), w)
	defer monitor.Close()
}

func TestMetadataPresentShutdown(t *testing.T) {
	dir := t.TempDir()
	metadataPath := filepath.Join(dir, "metadata.json")
	if err := ioutil.WriteFile(metadataPath, makeTestMetadata(10), 0o600); err != nil {
		t.Fatal(err)
	}

	w, err := fswatch.NewWatcher()
	if err != nil {
		t.Fatal(err)
	}
	defer w.Close()

	monitor := NewMetadataMonitor(metadataPath, w)
	defer monitor.Close()
}

func TestMetadataMonitor(t *testing.T) {
	defer pushPopMonitorPollInterval(20 * time.Millisecond)()
	dir := t.TempDir()
	metadataPath := filepath.Join(dir, "metadata.json")
	if err := ioutil.WriteFile(metadataPath, makeTestMetadata(1), 0o600); err != nil {
		t.Fatal(err)
	}

	w, err := fswatch.NewWatcher()
	if err != nil {
		t.Fatal(err)
	}
	defer w.Close()

	monitor := NewMetadataMonitor(metadataPath, w)
	defer monitor.Close()

	go func() {
		for event := range w.Events {
			monitor.HandleEvent(event)
		}
	}()

	t.Run("starts with no events", func(t *testing.T) {
		select {
		case metadata := <-monitor.Events:
			t.Fatalf("Unexpected event: %#v", metadata)
		case <-time.After(time.Millisecond):
		}
	})

	t.Run("writing with same version does not send an event", func(t *testing.T) {
		if err := ioutil.WriteFile(metadataPath, makeTestMetadata(1), 0o600); err != nil {
			t.Fatal(err)
		}

		select {
		case metadata := <-monitor.Events:
			t.Fatalf("Unexpected event: %#v", metadata)
		case <-time.After(time.Millisecond):
		}
	})

	t.Run("writing with different version does send an event", func(t *testing.T) {
		if err := ioutil.WriteFile(metadataPath, makeTestMetadata(2), 0o600); err != nil {
			t.Fatal(err)
		}

		metadata := <-monitor.Events
		if metadata.Version != 2 {
			t.Fatalf("got %v, want 2", metadata.Version)
		}
	})

	t.Run("delete and recreate sends an event", func(t *testing.T) {
		if err := os.Remove(metadataPath); err != nil {
			t.Fatal(err)
		}
		select {
		case metadata := <-monitor.Events:
			t.Fatalf("Unexpected event: %#v", metadata)
		case <-time.After(time.Millisecond):
		}

		if err := ioutil.WriteFile(metadataPath, makeTestMetadata(3), 0o600); err != nil {
			t.Fatal(err)
		}
		metadata := <-monitor.Events
		if metadata.Version != 3 {
			t.Fatalf("got %v, want 3", metadata.Version)
		}
	})

	t.Run("move and recreate sends an event", func(t *testing.T) {
		if err := os.Rename(metadataPath, metadataPath+".moved"); err != nil {
			t.Fatal(err)
		}
		select {
		case metadata := <-monitor.Events:
			t.Fatalf("Unexpected event: %#v", metadata)
		case <-time.After(time.Millisecond):
		}

		if err := ioutil.WriteFile(metadataPath, makeTestMetadata(4), 0o600); err != nil {
			t.Fatal(err)
		}
		metadata := <-monitor.Events
		if metadata.Version != 4 {
			t.Fatalf("got %v, want 4", metadata.Version)
		}
	})

}

func TestMetadataMonitorResetsStateOnInvalidData(t *testing.T) {
	defer pushPopMonitorPollInterval(20 * time.Millisecond)()
	dir := t.TempDir()
	metadataPath := filepath.Join(dir, "metadata.json")
	if err := ioutil.WriteFile(metadataPath, makeTestMetadata(1), 0o600); err != nil {
		t.Fatal(err)
	}

	w, err := fswatch.NewWatcher()
	if err != nil {
		t.Fatal(err)
	}
	defer w.Close()

	monitor := NewMetadataMonitor(metadataPath, w)
	defer monitor.Close()

	// Writing invalid metadata doesn't trigger an event, but the monitor does forget about the
	// previous contents.
	if err := ioutil.WriteFile(metadataPath, []byte("bad metadata"), 0o600); err != nil {
		t.Fatal(err)
	}
	monitor.HandleEvent(fswatch.Event{
		Name: metadataPath,
		Op:   fswatch.Write,
	})
	select {
	case metadata := <-monitor.Events:
		t.Fatalf("Unexpected event: %#v", metadata)
	case <-time.After(time.Millisecond):
	}

	// Rewriting the same file results in an event as it went from an invalid state to a valid
	// one.
	if err := ioutil.WriteFile(metadataPath, makeTestMetadata(1), 0o600); err != nil {
		t.Fatal(err)
	}
	go monitor.HandleEvent(fswatch.Event{
		Name: metadataPath,
		Op:   fswatch.Write,
	})
	metadata := <-monitor.Events
	if metadata.Version != 1 {
		t.Fatalf("got %v, want 1", metadata.Version)
	}
}

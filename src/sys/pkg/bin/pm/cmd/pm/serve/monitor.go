// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package serve

import (
	"encoding/json"
	"fmt"
	"io/ioutil"
	"log"
	"sync"
	"time"

	"go.fuchsia.dev/fuchsia/src/sys/pkg/bin/pm/fswatch"
)

// MetadataMonitor monitors the contents of a TUF metadata file at a given path, emitting an event
// whenever its version changes.
type MetadataMonitor struct {
	Events chan Metadata

	path string

	wg      sync.WaitGroup
	mu      sync.Mutex
	current *Metadata
	done    chan struct{}
	watcher *fswatch.Watcher
}

// NewMetadataMonitor monitors the given path containing valid TUF metadata and is resilient to the
// file at that location being deleted or moved, emitting a single event on the returned `Events`
// channel whenever the version field of the metadata changes, or the file at that path changes from
// missing/invalid to valid, even if that version was previously observed.
//
// The provided `watcher` object will be configured to watch `path` (and re-configured to watch that
// path if the file is deleted and re-created), but the user of this type must manually forward all
// observed events from the watcher for `path` to the `HandleEvent` method.
func NewMetadataMonitor(path string, watcher *fswatch.Watcher) *MetadataMonitor {
	m := &MetadataMonitor{
		Events:  make(chan Metadata),
		path:    path,
		done:    make(chan struct{}),
		watcher: watcher,
	}

	// Start up the state machine. The MetadataMonitor is either waiting for an event from the
	// filesystem watcher (as long as the file exists) or polling the path on a timer waiting
	// for it to exist and become watchable with the filesystem watcher.
	if err := watcher.Add(path); err != nil {
		m.goWaitForPath()
	} else {
		m.current, err = readMetadata(path)
		if err != nil {
			log.Printf("[pm auto] invalid initial metadata for %q: %v", path, err)
		}
	}

	return m
}

// Close stops monitoring the metadata file and waits for any associated goroutines to finish.
func (m *MetadataMonitor) Close() {
	// Signal that it is time to stop.
	m.mu.Lock()
	close(m.done)
	m.watcher = nil
	m.mu.Unlock()

	// Wait for things to stop and close the output channel.
	m.wg.Wait()
	close(m.Events)
}

// monitorPollInterval parameterized for tests.
var monitorPollInterval time.Duration = 500 * time.Millisecond

// pushPopMonitorPollInterval sets monitorPollInterval to the given value and returns a function to
// undo the operation. Pairs nicely with defer.
func pushPopMonitorPollInterval(interval time.Duration) func() {
	prev := monitorPollInterval
	monitorPollInterval = interval
	return func() {
		monitorPollInterval = prev
	}
}

func (m *MetadataMonitor) goWaitForPath() {
	m.wg.Add(1)
	go func() {
		defer m.wg.Done()
		ticker := time.NewTicker(monitorPollInterval)
		defer ticker.Stop()

		for {
			select {
			case <-m.done:
				return
			case <-ticker.C:
				m.mu.Lock()
				if m.isClosedLocked() {
					m.mu.Unlock()
					return
				}
				if err := m.watcher.Add(m.path); err != nil {
					m.mu.Unlock()
					continue
				}

				current, err := readMetadata(m.path)
				if err != nil {
					// The file is now being watched, but reading/parsing failed.  If it is invalid,
					// ignore the change. If it was deleted, let the next HandleEvent call with
					// that info respawn this goroutine.
					m.current = nil
					m.mu.Unlock()
					return
				}

				updated := m.current == nil || m.current.Version != current.Version
				m.current = current
				m.mu.Unlock()

				if updated {
					// Send the event, but if Close has already been called or is called while
					// waiting to send the event, drop the now unwanted event and return.
					select {
					case m.Events <- *current:
					case <-m.done:
					}

				}
				return
			}
		}
	}()
}

func (m *MetadataMonitor) isClosedLocked() bool {
	return m.watcher == nil
}

// HandleEvent must be called with any events received by the fswatch.Watcher and for the path
// provided to NewMetadataMonitor.
func (m *MetadataMonitor) HandleEvent(event fswatch.Event) {
	if event.Name != m.path {
		return
	}

	switch event.Op {
	case fswatch.Remove:
		m.mu.Lock()
		defer m.mu.Unlock()
		if !m.isClosedLocked() {
			if m.watcher.Remove(m.path) == nil {
				log.Printf("[pm auto] ERROR: expected watch to no longer exist: %q", m.path)
			}
			m.goWaitForPath()
		}
	case fswatch.Rename:
		m.mu.Lock()
		defer m.mu.Unlock()
		if !m.isClosedLocked() {
			// The watch still exists on the moved file, but fsnotify incorrectly ignores those events.
			// Remove the watch and let it be re-created when the desired path exists again.
			if err := m.watcher.Remove(m.path); err != nil {
				log.Printf("[pm auto] ERROR: unable to remove watch for %q: %v", m.path, err)
			}
			m.goWaitForPath()
		}
	default:
		m.mu.Lock()

		current, err := readMetadata(m.path)
		if err != nil {
			m.current = nil
			m.mu.Unlock()
			return
		}

		updated := m.current == nil || m.current.Version != current.Version
		m.current = current
		m.mu.Unlock()

		// Send the event, but if Close has already been called or is called while waiting to
		// send the event, drop the now unwanted event and return.
		if updated {
			select {
			case m.Events <- *current:
			case <-m.done:
			}

		}
	}

}

type Metadata struct {
	Version int       `json:"version"`
	Expires time.Time `json:"expires"`
}

// fswatch notifies on write, but there may be more writes on the way.  Very
// badly wait for the file to be fully written by trying to read/parse the
// metadata until it works, or we give up trying.
func readMetadata(path string) (*Metadata, error) {
	var err error
	var metadata *Metadata

	// 15 attempts is a somewhat arbitrary value, chosen by slowing down the
	// tests' metadata writes to write a single byte at a time, and increasing
	// this number until a --test.count 100 test pass didn't flake.
	//
	// A more correct fix to this issue would be to have an event to consume
	// when a file is modified and then closed by the writer.  This hack hopes
	// to cheaply reduce the flake rate by adding a retry to the metadata
	// reader until a more proper fix can be implemented, or pm is replaced
	// with a different tool.
	for i := 0; i < 15; i++ {
		metadata, err = readMetadataAttempt(path)
		if err == nil {
			return metadata, nil
		}
	}
	return nil, err
}

func readMetadataAttempt(path string) (*Metadata, error) {
	contents, err := ioutil.ReadFile(path)
	if err != nil {
		return nil, fmt.Errorf("unable to read %q: %w", path, err)
	}

	var metadata struct {
		Signed Metadata `json:"signed"`
	}
	if err := json.Unmarshal(contents, &metadata); err != nil {
		return nil, fmt.Errorf("unable to parse contents of %q: %w", path, err)
	}

	return &metadata.Signed, nil
}

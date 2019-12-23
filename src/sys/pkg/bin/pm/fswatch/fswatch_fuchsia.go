// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//+build fuchsia

package fswatch

import (
	"os"
	"sync"
	"time"

	"github.com/fsnotify/fsnotify"
)

// TODO(raggi): implement this using the real filesystem watcher API
type Watcher struct {
	Events chan fsnotify.Event
	Errors chan error

	mu    sync.Mutex
	times map[string]time.Time
}

func NewWatcher() (*Watcher, error) {
	w := &Watcher{
		Events: make(chan fsnotify.Event),
		Errors: make(chan error),
		times:  map[string]time.Time{},
	}
	go func() {
		for range time.NewTicker(time.Second).C {
			w.mu.Lock()
			paths := make([]string, 0, len(w.times))
			for path := range w.times {
				paths = append(paths, path)
			}
			w.mu.Unlock()

			for _, path := range paths {
				if fi, err := os.Stat(path); err == nil {
					t := fi.ModTime()
					sendEvent := false
					w.mu.Lock()
					if t != w.times[path] {
						sendEvent = true
						w.times[path] = t
					}
					w.mu.Unlock()
					if sendEvent {
						w.Events <- fsnotify.Event{
							Name: path,
							Op:   fsnotify.Write,
						}
					}
				}
			}
		}
	}()

	return w, nil
}

func (w *Watcher) Add(path string) error {
	w.mu.Lock()
	defer w.mu.Unlock()
	fi, err := os.Stat(path)
	if err != nil {
		return err
	}
	w.times[path] = fi.ModTime()
	return nil
}

func (w *Watcher) Close() error {
	w.mu.Lock()
	defer w.mu.Unlock()

	w.times = map[string]time.Time{}
	close(w.Events)
	return nil
}

func (w *Watcher) Remove(path string) error {
	w.mu.Lock()
	defer w.mu.Unlock()
	delete(w.times, path)
	return nil
}

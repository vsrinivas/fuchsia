// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package serve

import (
	"bufio"
	"fmt"
	"log"
	"os"
	"sync"
	"time"

	"go.fuchsia.dev/fuchsia/src/sys/pkg/bin/pm/fswatch"
)

// Delay to wait for more fswatch events before requesting a repo publish/commit
const BatchProcessingDelay = 30 * time.Millisecond

// Polling time between checks for non-existent manifests
const NonExistentPollingTime = 1 * time.Second

type ManifestWatcher struct {
	PublishEvents   chan []string
	watcher         *fswatch.Watcher
	ticker          *time.Ticker
	tickerDone      chan bool
	missing         MissingManifests
	batch           *ManifestBatch
	publishList     *string
	quiet           *bool
	runningRoutines sync.WaitGroup
}

type ManifestBatch struct {
	m  map[string]bool
	t  *time.Timer
	mu sync.Mutex
}

type MissingManifests struct {
	sync.RWMutex
	m map[string]bool
}

func NewManifestWatcher(publishList *string, quiet *bool) (*ManifestWatcher, error) {
	watcher, err := fswatch.NewWatcher()
	if err != nil {
		return nil, fmt.Errorf("failed to initialize fsnotify: %s", err)
	}
	mw := &ManifestWatcher{
		watcher:     watcher,
		publishList: publishList,
		quiet:       quiet,
		batch:       &ManifestBatch{m: make(map[string]bool)},
	}
	mw.PublishEvents = make(chan []string)
	mw.missing.m = make(map[string]bool)
	return mw, nil
}

func (mw *ManifestWatcher) publishManifests(pkgManifestPaths []string) {
	mw.logV("[pm incremental] publishManifests with %d packages", len(pkgManifestPaths))
	validManifests := make([]string, 0, len(pkgManifestPaths))
	for i, m := range pkgManifestPaths {
		if _, err := os.Stat(m); err != nil {
			// manifest does not exist, just add to missingManifests
			mw.missing.Lock()
			mw.missing.m[m] = true
			mw.missing.Unlock()
		} else {
			// manifest exists, remove from missingManifests
			validManifests = append(validManifests, pkgManifestPaths[i])
			mw.missing.RLock()
			_, found := mw.missing.m[m]
			mw.missing.RUnlock()
			if found {
				mw.missing.Lock()
				delete(mw.missing.m, m)
				mw.missing.Unlock()
			}
		}
	}
	if len(validManifests) == 0 {
		return
	}

	mw.PublishEvents <- validManifests

	for _, m := range validManifests {
		if err := mw.watcher.Add(m); err != nil {
			log.Printf("ERROR: unable to watch %q", m)
		}
	}
}

func (mw *ManifestWatcher) publishManifestList() error {
	mw.logV("[pm incremental] publishing %q", *mw.publishList)
	f, err := os.Open(*mw.publishList)
	if err != nil {
		return fmt.Errorf("[pm incremental] cannot read list of package manifests %q: %s", *mw.publishList, err)
	}
	defer f.Close()

	mw.missing.Lock()
	mw.missing.m = make(map[string]bool)
	mw.missing.Unlock()

	pkgManifestPaths := make([]string, 0, 100)
	s := bufio.NewScanner(f)
	for s.Scan() {
		m := s.Text()
		pkgManifestPaths = append(pkgManifestPaths, m)
	}
	if err := s.Err(); err != nil {
		return fmt.Errorf("[pm incremental] cannot read list of package manifests %q: %s", *mw.publishList, err)
	}
	mw.logV("[pm incremental] read from manifestList %d packages", len(pkgManifestPaths))
	mw.publishManifests(pkgManifestPaths)
	return nil
}

func (mw *ManifestWatcher) processQueue() {
	mw.batch.mu.Lock()
	mw.batch.t.Stop()
	mw.batch.t = nil
	if len(mw.batch.m) == 0 {
		mw.batch.mu.Unlock()
		return
	}
	filenames := make([]string, 0, len(mw.batch.m))
	for k := range mw.batch.m {
		filenames = append(filenames, k)
	}
	mw.batch.m = make(map[string]bool)
	mw.batch.mu.Unlock()
	mw.publishManifests(filenames)
}

func (mw *ManifestWatcher) enqueue(ms ...string) {
	mw.batch.mu.Lock()
	defer mw.batch.mu.Unlock()
	for _, m := range ms {
		mw.batch.m[m] = true
		mw.logV("[pm incremental] manifest created or changed, enqueuing: %s", m)
	}
	if mw.batch.t != nil {
		mw.batch.t.Stop()
		mw.batch.t.Reset(BatchProcessingDelay)
	} else {
		mw.batch.t = time.AfterFunc(BatchProcessingDelay, func() {
			mw.runningRoutines.Add(1)
			defer mw.runningRoutines.Done()
			mw.processQueue()
		})
	}
}

func (mw *ManifestWatcher) startMissingFilesPolling() {
	// monitor non-existent manifests via polling every second
	// TODO: evaluate if there's a way to watch non-existent files that
	// doesn't involve polling. Watching the build output root directory
	// is quite expensive since hundreds of thousands of files are created
	// during a full build.
	mw.ticker = time.NewTicker(NonExistentPollingTime)
	mw.tickerDone = make(chan bool)
	mw.runningRoutines.Add(1)
	go func() {
		defer mw.runningRoutines.Done()
		for {
			select {
			case <-mw.tickerDone:
				return
			case <-mw.ticker.C:
				mw.missing.Lock()
				if len(mw.missing.m) == 0 {
					mw.missing.Unlock()
					continue
				}
				ms := make([]string, 0)
				for m := range mw.missing.m {
					if _, err := os.Stat(m); err == nil {
						ms = append(ms, m)
						delete(mw.missing.m, m)
					}
				}
				mw.missing.Unlock()
				if len(ms) > 0 {
					mw.enqueue(ms...)
				}
			}
		}
	}()
}

func (mw *ManifestWatcher) start() error {
	if mw.watcher == nil {
		return fmt.Errorf("Unexpected error, ManifestWatcher invalid state")
	}
	if err := mw.watcher.Add(*mw.publishList); err != nil {
		return fmt.Errorf("failed to watch %s: %s", *mw.publishList, err)
	}

	mw.startMissingFilesPolling()

	mw.runningRoutines.Add(1)
	go func() {
		defer mw.runningRoutines.Done()
		for event := range mw.watcher.Events {
			switch event.Name {
			case *mw.publishList:
				if event.Op == fswatch.Remove {
					log.Printf("[pm incremental] WARNING: list of manifests removed, "+
						"it won't be watched anymore. Restart to resume watching it for changes: %s", event.Name)
					continue
				}
				mw.logV("[pm incremental] list of manifests modified (%s), attempting to republish all from %q", event.Op, event.Name)
				if err := mw.publishManifestList(); err != nil {
					log.Printf("[pm incremental] WARNING: error while publishing list of manifests: %s", err)
				}
			default:
				if event.Op == fswatch.Remove {
					mw.logV("[pm incremental] manifest %q removed, adding to polling watcher", event.Name)
					mw.missing.Lock()
					mw.missing.m[event.Name] = true
					mw.missing.Unlock()
				} else {
					mw.logV("[pm incremental] manifest %s, event %s", event.Name, event.Op)
					mw.enqueue(event.Name)
				}
			}
		}
	}()
	return mw.publishManifestList()
}

func (mw *ManifestWatcher) logV(format string, v ...interface{}) {
	if !*mw.quiet {
		log.Printf(format, v...)
	}
}

func (mw *ManifestWatcher) stop() {
	mw.logV("Debug: ManifestWatcher stopping")
	if mw.batch != nil {
		if mw.batch.t != nil {
			mw.batch.t.Stop()
			mw.batch.t = nil
		}
		mw.batch = nil
	}
	if mw.ticker != nil {
		mw.ticker.Stop()
		mw.tickerDone <- true
		mw.ticker = nil
	}
	if mw.watcher != nil {
		mw.watcher.Close()
		mw.watcher = nil
	}
	mw.runningRoutines.Wait()
	close(mw.PublishEvents)
}

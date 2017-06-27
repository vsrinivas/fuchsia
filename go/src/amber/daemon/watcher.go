// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package daemon

import (
	"fmt"
	"io/ioutil"
	"path/filepath"
	"strings"
	"sync"
	"time"
)

// WatchNeeds listens to the supplied channels to run monitor for, and retrieve
// new packages. When an event arrives on the 'shrt' channel, any new needs are
// immediately requested from the Daemon. When an event is received on the 'lng'
// channel any previously new needs which have not be fulfilled are requested
// from the Daemon. When an event is received on the 'stp' channel or the
// channel is closed, this function will return.
func WatchNeeds(d *Daemon, shrt <-chan time.Time, lng <-chan time.Time,
	stp <-chan struct{}, needsPath string) {
	oldPkgs := make(map[Package]struct{})
	oldBlobs := make(map[string]struct{})
	muBlob := &sync.Mutex{}

	for {
		select {
		case _, ok := <-shrt:
			if ok {
				newPkgs, newBlobs := processUpdate(oldPkgs, oldBlobs, muBlob, needsPath)
				if newPkgs != nil {
					d.GetUpdates(newPkgs)
				}

				for _, blob := range newBlobs {
					go getBlob(d, blob, muBlob, oldBlobs)
				}
			}
		case _, ok := <-lng:
			if ok {
				if len(oldPkgs) > 0 {
					getPkgs(d, oldPkgs)
				}
			}
		case _, _ = <-stp:
			return

		}
	}
}

func getBlob(d *Daemon, newBlob string, muBlob *sync.Mutex,
	currentBlobs map[string]struct{}) {
	// TODO(jmatt) Consider using shorter delay if no error
	d.GetBlob(newBlob)

	go func(mu *sync.Mutex, blobSet map[string]struct{}, targ string) {
		time.Sleep(5 * time.Minute)
		mu.Lock()
		defer mu.Unlock()
		delete(blobSet, targ)
	}(muBlob, currentBlobs, newBlob)
}

// getPkgs requests the supplied set of Packages from the supplied Daemon.
func getPkgs(d *Daemon, known map[Package]struct{}) {
	ps := NewPackageSet()
	for k, _ := range known {
		tuf_name := fmt.Sprintf("/%s", k.Name)
		ps.Add(&Package{Name: tuf_name, Version: k.Version})
	}
	d.GetUpdates(ps)
}

// processUpdate looks the pacakge needs directory and compares this against the
// passed in Package set. Any new needs are added to the set and any entries in
// map not represented in the directory are removed. New needs are also returned
// as a PackageSet which is returned. If there are no new needs nil is returned.
func processUpdate(known map[Package]struct{}, oldBlobs map[string]struct{}, muBlobs *sync.Mutex, needsPath string) (*PackageSet, []string) {
	muBlobs.Lock()
	defer muBlobs.Unlock()
	current := make(map[Package]struct{})
	names, err := ioutil.ReadDir(needsPath)
	if err != nil {
		return nil, nil
	}

	blobReqs := make(map[string]struct{})

	for _, entName := range names {
		if entName.IsDir() {
			pkgPath := filepath.Join(needsPath, entName.Name())
			versions, err := ioutil.ReadDir(pkgPath)
			if err != nil {
				fmt.Printf("Error reading contents of directory '%s'\n", pkgPath)
				continue
			}
			for _, version := range versions {
				pkg := Package{Name: entName.Name(), Version: version.Name()}
				current[pkg] = struct{}{}
			}
		} else {
			if strings.Contains(entName.Name(), ".") {
				pkg := Package{Name: entName.Name(), Version: ""}
				current[pkg] = struct{}{}
			} else {
				// TODO(jmatt) validate only hex characters
				blobReqs[entName.Name()] = struct{}{}
			}
		}
	}

	for k, _ := range known {
		if _, ok := current[k]; !ok {
			delete(known, k)
		} else {
			delete(current, k)
		}
	}

	newBlobs := []string{}

	for k, _ := range oldBlobs {
		// IF a previously seen blob went away, remove it
		// ELSE remove previously seen from new request set
		if _, ok := blobReqs[k]; !ok {
			delete(oldBlobs, k)
		} else {
			delete(blobReqs, k)
		}
	}

	// Add any remaining new blob requests to the old blob set and list
	// to be returned
	for k, _ := range blobReqs {
		oldBlobs[k] = struct{}{}
		newBlobs = append(newBlobs, k)
	}

	var ps *PackageSet = nil
	if len(current) > 0 {
		ps = NewPackageSet()
		for k, _ := range current {
			tuf_name := fmt.Sprintf("/%s", k.Name)
			ps.Add(&Package{Name: tuf_name, Version: k.Version})
			known[k] = struct{}{}
		}
	}

	return ps, newBlobs
}

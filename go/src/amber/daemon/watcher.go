// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package daemon

import (
	"fmt"
	"io/ioutil"
	"path/filepath"
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
	oNeeds := make(map[Package]struct{})

	for {
		select {
		case _, ok := <-shrt:
			if ok {
				newNeeds := processUpdate(oNeeds, needsPath)
				if newNeeds != nil {
					d.GetUpdates(newNeeds)
				}
			}
		case _, ok := <-lng:
			if ok {
				if len(oNeeds) > 0 {
					getPkgs(d, oNeeds)
				}
			}
		case _, _ = <-stp:
			return

		}
	}
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
func processUpdate(known map[Package]struct{}, needsPath string) *PackageSet {
	current := make(map[Package]struct{})
	names, err := ioutil.ReadDir(needsPath)
	if err != nil {
		fmt.Println("Couldn't read directory")
		return nil
	}

	for _, pkgName := range names {
		if pkgName.IsDir() {
			pkgPath := filepath.Join(needsPath, pkgName.Name())
			versions, err := ioutil.ReadDir(pkgPath)
			if err != nil {
				fmt.Printf("Error reading contents of directory '%s'\n", pkgPath)
				continue
			}
			for _, version := range versions {
				pkg := Package{Name: pkgName.Name(), Version: version.Name()}
				current[pkg] = struct{}{}
			}
		} else {
			pkg := Package{Name: pkgName.Name(), Version: ""}
			current[pkg] = struct{}{}
		}
	}

	for k, _ := range known {
		if _, ok := current[k]; !ok {
			delete(known, k)
		} else {
			delete(current, k)
		}
	}

	if len(current) > 0 {
		ps := NewPackageSet()
		for k, _ := range current {
			tuf_name := fmt.Sprintf("/%s", k.Name)
			ps.Add(&Package{Name: tuf_name, Version: k.Version})
			known[k] = struct{}{}
		}
		return ps
	}

	return nil
}

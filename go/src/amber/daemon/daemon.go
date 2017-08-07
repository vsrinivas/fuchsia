// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package daemon

import (
	"errors"
	"fmt"
	"io"
	"os"
	"path/filepath"
	"strings"
	"sync"
	"time"
)

var DstUpdate = "/pkgfs/incoming"
var DstBlob = DstUpdate
var newTicker = time.NewTicker

// ErrSrcNotFound is returned when a request is made to RemoveSource, but the
// passed in Source is not known to the Daemon
var ErrSrcNotFound = errors.New("amber/daemon: no corresponding source found")

// Deamon provides access to a set of Sources and oversees the polling of those
// Sources for updates to Packages in the supplied PackageSet. GetUpdates can be
// used for a one-and-done set of packages.
//
// Note that methods on this struct are not designed for parallel access.
// Execution contexts sharing a single Daemon instance should mediate access
// to all calls into the Daemon.
type Daemon struct {
	srcMons  []*SourceMonitor
	pkgs     *PackageSet
	runCount sync.WaitGroup
	// takes an update Package, an original Package and a Source to get the
	// update Package content from.
	processor func(*Package, *Package, Source, *PackageSet) error
	// sources must claim this before running updates
	updateLock sync.Mutex

	//blobSrc       *BlobFetcher
	muBlobUpdates sync.Mutex

	muRepos sync.Mutex
	repos   []BlobRepo
}

// NewDaemon creates a Daemon with the given SourceSet
func NewDaemon(r *PackageSet, f func(*Package, *Package, Source, *PackageSet) error) *Daemon {
	d := &Daemon{pkgs: r,
		updateLock: sync.Mutex{},
		srcMons:    []*SourceMonitor{},
		processor:  f,
		repos:      []BlobRepo{}}
	return d
}

// AddSource is called to add a Source that can be used to get updates. When the
// Source is added, the Daemon will start polling it at the interval from
// Source.GetInterval()
func (d *Daemon) AddSource(s Source) {
	mon := NewSourceMonitor(s, d.pkgs, d.processor, &d.updateLock)
	d.srcMons = append(d.srcMons, mon)
	d.runCount.Add(1)
	go func() {
		defer d.runCount.Done()
		mon.Run()
	}()
}

func (d *Daemon) blobRepos() []BlobRepo {
	d.muRepos.Lock()
	defer d.muRepos.Unlock()
	c := make([]BlobRepo, len(d.repos))
	copy(c, d.repos)
	return c
}

func (d *Daemon) AddBlobRepo(br BlobRepo) {
	d.muRepos.Lock()
	d.repos = append(d.repos, br)
	d.muRepos.Unlock()
}

// GetBlobs is a blocking call which tries to get all requested blobs
func (d *Daemon) GetBlob(blob string) error {
	repos := d.blobRepos()
	return FetchBlob(repos, blob, &d.muBlobUpdates, DstBlob)
}

func (d *Daemon) RemoveBlobRepo(r BlobRepo) {
	d.muRepos.Lock()
	for i, _ := range d.repos {
		if d.repos[i] == r {
			d.repos = append(d.repos[:i], d.repos[i+1:]...)
			break
		}
	}
	d.muRepos.Unlock()
}

// GetUpdates queries all available Sources for the supplied PackageSet and
// returns true if at least one Source accepted the query. The Packages in the
// PackageSet will not participate in any on-going monitoring for updates, this
// will just be a one-time check.
func (d *Daemon) GetUpdates(pkgs *PackageSet) bool {
	inProgress := false
	for _, src := range d.srcMons {
		if e := src.GetUpdates(pkgs); e == nil {
			inProgress = true
		}
	}
	return inProgress
}

// RemoveSource should be used to stop using a Source previously added with
// AddSource. This method does not wait for any in-progress polling operation
// on the Source to complete. This method returns ErrSrcNotFound if the supplied
// Source is not know to this Daemon.
func (d *Daemon) RemoveSource(src Source) error {
	for i, m := range d.srcMons {
		if m.src.Equals(src) {
			d.srcMons = append(d.srcMons[:i], d.srcMons[i+1:]...)
			m.Stop()
			return nil
		}

	}
	return ErrSrcNotFound
}

// CancelAll stops all update retrieval operations, blocking until any
// in-progress operations complete.
func (d *Daemon) CancelAll() {
	//d.blobSrc.Stop()
	for _, s := range d.srcMons {
		s.Stop()
	}

	d.runCount.Wait()
	d.srcMons = []*SourceMonitor{}
}

// ErrProcPkgIO is a general I/O error during ProcessPackage
type ErrProcessPackage string

func NewErrProcessPackage(f string, args ...interface{}) error {
	return ErrProcessPackage(fmt.Sprintf("processor: %s", fmt.Sprintf(f, args...)))
}

func (e ErrProcessPackage) Error() string {
	return string(e)
}

// ProcessPackage attempts to retrieve the content of the supplied Package
// from the supplied Source. If retrieval from the Source fails, the Source's
// error is returned. If there is a local I/O error when processing the package
// an ErrProcPkgIO is returned.
func ProcessPackage(update *Package, orig *Package, src Source, pkgs *PackageSet) error {
	// TODO(jmatt) Checking is needed to validate that the original package
	// hasn't already been updated.

	// this package processor can only deal with names that look like
	// file paths
	// TODO(jmatt) better checking that this could be a valid file path
	if strings.Index(update.Name, "/") != 0 {
		return NewErrProcessPackage("invalid path")
	}

	file, e := src.FetchPkg(update)
	if e != nil {
		return e
	}
	defer file.Close()
	defer os.Remove(file.Name())

	dstPath := filepath.Join(DstUpdate, update.Name)
	dst, e := os.Create(dstPath)
	if e != nil {
		return NewErrProcessPackage("couldn't open file to write update %v", e)
	}
	defer dst.Close()

	i, err := file.Stat()
	if err != nil {
		return NewErrProcessPackage("couldn't stat temp file", e)
	}

	err = dst.Truncate(i.Size())
	if err != nil {
		return NewErrProcessPackage("couldn't truncate file destination", e)
	}
	_, e = io.Copy(dst, file)
	// TODO(jmatt) validate file on disk, size, hash, etc
	if e != nil {
		return NewErrProcessPackage("couldn't write update to file %v", e)
	}

	pkgs.Replace(orig, update, false)
	return nil
}

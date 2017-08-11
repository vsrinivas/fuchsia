// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package daemon

import (
	"errors"
	"fmt"
	"io"
	"log"
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

const CheckInterval = 5 * time.Minute

// Deamon provides access to a set of Sources and oversees the polling of those
// Sources for updates to Packages in the supplied PackageSet. GetUpdates can be
// used for a one-and-done set of packages.
//
// Note that methods on this struct are not designed for parallel access.
// Execution contexts sharing a single Daemon instance should mediate access
// to all calls into the Daemon.
type Daemon struct {
	srcMons  []*SourceMonitor
	srcs     []Source
	pkgs     *PackageSet
	runCount sync.WaitGroup
	// takes an update Package, an original Package and a Source to get the
	// update Package content from.
	processor func(*GetResult, *PackageSet) error

	//blobSrc       *BlobFetcher
	muBlobUpdates sync.Mutex

	muRepos sync.Mutex
	repos   []BlobRepo

	muInProg sync.Mutex
	inProg   map[Package]*upRec
}

// NewDaemon creates a Daemon with the given SourceSet
func NewDaemon(r *PackageSet, f func(*GetResult, *PackageSet) error) *Daemon {
	d := &Daemon{pkgs: r,
		runCount:  sync.WaitGroup{},
		srcMons:   []*SourceMonitor{},
		processor: f,
		repos:     []BlobRepo{},
		muInProg:  sync.Mutex{},
		inProg:    make(map[Package]*upRec)}
	mon := NewSourceMonitor(d, r, f, CheckInterval)
	d.runCount.Add(1)
	d.srcMons = append(d.srcMons, mon)
	go func() {
		mon.Run()
		d.runCount.Done()
	}()
	return d
}

// AddSource is called to add a Source that can be used to get updates. When the
// Source is added, the Daemon will start polling it at the interval from
// Source.GetInterval()
func (d *Daemon) AddSource(s Source) {
	// TODO(jmatt) make GetUpdates thread-safe with respect to sources
	s = NewSourceKeeper(s)
	d.srcs = append(d.srcs, s)
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

type upRec struct {
	pkgs []*Package
	c    []chan<- map[Package]*GetResult
}

// GetResult represents the result of a package update request. To read the
// contents of the update call Open() and when done call Close(). These calls
// will Lock and Unlock to MuFile member to provide exclusive access to the
// underlying file.
type GetResult struct {
	*os.File
	Orig   Package
	Update Package
	MuFile sync.Mutex
	Err    error
}

func (g *GetResult) Open() error {
	g.MuFile.Lock()
	f, err := os.Open(g.File.Name())
	if err != nil {
		g.MuFile.Unlock()
	} else {
		g.File = f
	}
	return err
}

func (g *GetResult) Close() error {
	err := g.File.Close()
	defer g.MuFile.Unlock()
	return err
}

type pkgSrcPair struct {
	pkgs map[Package]Package
	src  Source
}

func (d *Daemon) awaitResults(c chan map[Package]*GetResult, awaiting []*Package,
	out map[Package]*GetResult, mu *sync.Mutex) {
	updates := <-c
	mu.Lock()
	for _, p := range awaiting {
		if up, ok := updates[*p]; ok {
			out[*p] = up
		}
	}
	mu.Unlock()
}

// GetUpdates queries all available sources for updates to the Packages in the
// supplied PackageSet. This call blocks until any requested updates are
// downloaded. The returned map of Packages to GetResults can then be used to
// retrieve the updates themselves. Updates whose files are not claimed in a
// reasonable period will be automatically deleted. Callers should check the
// GetResult's Err member to see if the update attempt was successful.
func (d *Daemon) GetUpdates(pkgs *PackageSet) map[Package]*GetResult {
	// GetUpdates provides safe concurrent fetching from multiple update
	// sources. To do this we maintain a map in Daemon, inProg, which
	// contains enties for any package which is currently being updated.
	// When GetUpdates is called we first see if any of the packages passed
	// in is already being updated. If so, we subscribe to its results by
	// adding this instance's reply channel to the list of channels for
	// that result. For any requested packages that aren't already being
	// updated we create an entry in inProg for that set, add our reply
	// channel to that entry, and then call getUpdates to actually fetch
	// that set. For each update operation whose results we subscribe to
	// (including the one this call might start) we start a go routine to
	// await those results. Calls to GetUpdates then block until all go
	// routines terminate.
	totalRes := make(map[Package]*GetResult)
	resLock := sync.Mutex{}
	// LOCK ACQUIRE
	d.muInProg.Lock()
	toGet := []*Package{}
	wg := &sync.WaitGroup{}

	// see which packages we might already be fetching and just subscribe
	// to that result if we are
	c := make(chan map[Package]*GetResult)
	wanted := pkgs.Packages()
	for _, pkg := range wanted {
		if r, ok := d.inProg[*pkg]; ok {
			found := false
			// TODO(jmatt) work backward, since we are most likely
			// to be last in the list
			for _, rChan := range r.c {
				if rChan == c {
					found = true
					break
				}
			}
			// if we were not already subscribed to this result,
			// subscribe
			if !found {
				wg.Add(1)
				go func() {
					d.awaitResults(c, wanted, totalRes, &resLock)
					wg.Done()
				}()
				// note that we'll always receive the results
				// because before they are delivered to all
				// subscribers muInProg is locked and the in
				// progress entries are deleted before it is
				// unlocked
				r.c = append(r.c, c)
			}
		} else {
			toGet = append(toGet, pkg)
		}
	}

	var rec *upRec

	if len(toGet) > 0 {
		rec = &upRec{
			pkgs: toGet,
			c:    make([]chan<- map[Package]*GetResult, 0, 0),
		}
		wg.Add(1)
		go func() {
			d.awaitResults(c, wanted, totalRes, &resLock)
			wg.Done()
		}()
		// append our own channel
		rec.c = append(rec.c, c)
		for _, pkg := range rec.pkgs {
			d.inProg[*pkg] = rec
		}
	}
	// LOCK RELEASE
	d.muInProg.Unlock()

	if len(toGet) > 0 {
		fetchResults := d.getUpdates(rec)
		// LOCK ACQUIRE
		d.muInProg.Lock()

		// update is finished, delete ourselves from the in-progress map
		for _, pkg := range rec.pkgs {
			delete(d.inProg, *pkg)
		}

		for _, c := range rec.c {
			c <- fetchResults
		}
		// LOCK RELEASE
		d.muInProg.Unlock()
	}
	// wait for all the results to come back
	wg.Wait()

	// close our reply channel
	close(c)
	return totalRes
}

func (d *Daemon) getUpdates(rec *upRec) map[Package]*GetResult {
	fetchRecs := []*pkgSrcPair{}

	unfoundPkgs := rec.pkgs
	// TODO thread-safe access for sources?
	for i := 0; i < len(d.srcs) && len(unfoundPkgs) > 0; i++ {
		updates, err := d.srcs[i].AvailableUpdates(unfoundPkgs)
		if len(updates) == 0 || err != nil {
			if err == ErrRateExceeded {
				log.Printf("Source rate limit exceeded\n")
			}
			continue
		}

		pkgsToSrc := pkgSrcPair{
			pkgs: make(map[Package]Package),
			src:  d.srcs[i],
		}
		fetchRecs = append(fetchRecs, &pkgsToSrc)

		remaining := make([]*Package, 0, len(unfoundPkgs)-len(updates))
		for _, p := range unfoundPkgs {
			if u, ok := updates[*p]; ok {
				pkgsToSrc.pkgs[*p] = u
			} else {
				remaining = append(remaining, p)
			}
		}
		unfoundPkgs = remaining
	}

	// schedule our temp files to be cleaned up, if the requester doesn't
	// claim them, they can ask for the updates again
	files := make([]*os.File, 0, len(fetchRecs))
	defer func() {
		if len(files) > 0 {
			go cleanupFiles(files)
		}
	}()
	results := make(map[Package]*GetResult)
	for _, w := range fetchRecs {
		for p, u := range w.pkgs {
			res := GetResult{Orig: p, Update: u, MuFile: sync.Mutex{}}
			res.File, res.Err = w.src.FetchPkg(&u)
			if res.Err == nil {
				files = append(files, res.File)
				res.File.Close()
			}
			results[p] = &res
		}
	}

	if len(unfoundPkgs) > 0 {
		log.Printf("Some packages were not found %d\n", len(unfoundPkgs))
	}

	for _, pkg := range unfoundPkgs {
		res := GetResult{
			Orig: *pkg,
			File: nil,
			Err:  NewErrProcessPackage("update not found"),
		}
		results[*pkg] = &res
	}
	return results
}

func cleanupFiles(files []*os.File) {
	// TODO(jmatt) something better for cleanup
	time.Sleep(5 * time.Minute)
	for _, f := range files {
		os.Remove(f.Name())
	}
}

// RemoveSource should be used to stop using a Source previously added with
// AddSource. This method does not wait for any in-progress polling operation
// on the Source to complete. This method returns ErrSrcNotFound if the supplied
// Source is not know to this Daemon.
func (d *Daemon) RemoveSource(src Source) error {
	for i, m := range d.srcs {
		if m.Equals(src) {
			d.srcs = append(d.srcs[:i], d.srcs[i+1:]...)
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
func ProcessPackage(data *GetResult, pkgs *PackageSet) error {
	if data.Err != nil {
		return data.Err
	}
	// TODO(jmatt) Checking is needed to validate that the original package
	// hasn't already been updated.

	// this package processor can only deal with names that look like
	// file paths
	// TODO(jmatt) better checking that this could be a valid file path
	if strings.Index(data.Update.Name, "/") != 0 {
		return NewErrProcessPackage("invalid path")
	}

	e := data.Open()
	if e != nil {
		return e
	}
	defer data.Close()

	dstPath := filepath.Join(DstUpdate, data.Update.Name)
	dst, e := os.Create(dstPath)
	if e != nil {
		return NewErrProcessPackage("couldn't open file to write update %v", e)
	}
	defer dst.Close()

	i, err := data.Stat()
	if err != nil {
		return NewErrProcessPackage("couldn't stat temp file", e)
	}

	err = dst.Truncate(i.Size())
	if err != nil {
		return NewErrProcessPackage("couldn't truncate file destination", e)
	}
	_, e = io.Copy(dst, data)
	// TODO(jmatt) validate file on disk, size, hash, etc
	if e != nil {
		return NewErrProcessPackage("couldn't write update to file %v", e)
	}

	pkgs.Replace(&data.Orig, &data.Update, false)
	return nil
}

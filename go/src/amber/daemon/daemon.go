// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package daemon

import (
	"errors"
	"fmt"
	"io"
	"io/ioutil"
	"log"
	"net/url"
	"os"
	"path/filepath"
	"strings"
	"sync"
	"time"

	"amber/pkg"
	"amber/source"

	"fidl/fuchsia/amber"
)

var DstUpdate = "/pkgfs/install/pkg"
var DstBlob = "/pkgfs/install/blob"
var newTicker = time.NewTicker

// ErrSrcNotFound is returned when a request is made to RemoveSource, but the
// passed in Source is not known to the Daemon
var ErrSrcNotFound = errors.New("amber/daemon: no corresponding source found")

const CheckInterval = 5 * time.Minute

// Daemon provides access to a set of Sources and oversees the polling of those
// Sources for updates to Packages in the supplied PackageSet. GetUpdates can be
// used for a one-and-done set of packages.
//
// Note that methods on this struct are not designed for parallel access.
// Execution contexts sharing a single Daemon instance should mediate access
// to all calls into the Daemon.
type Daemon struct {
	store    string
	srcMons  []*SourceMonitor
	muSrcs   sync.Mutex
	srcs     map[string]source.Source
	pkgs     *pkg.PackageSet
	runCount sync.WaitGroup
	// takes an update Package, an original Package and a Source to get the
	// update Package content from.
	processor func(*GetResult, *pkg.PackageSet) error

	muRepos sync.Mutex
	repos   []BlobRepo

	muInProg sync.Mutex
	inProg   map[pkg.Package]*upRec
}

// NewDaemon creates a Daemon with the given SourceSet
func NewDaemon(store string, r *pkg.PackageSet, f func(*GetResult, *pkg.PackageSet) error,
	s []source.Source) (*Daemon, error) {
	d := &Daemon{
		store:     store,
		pkgs:      r,
		runCount:  sync.WaitGroup{},
		srcMons:   []*SourceMonitor{},
		srcs:      make(map[string]source.Source),
		processor: f,
		repos:     []BlobRepo{},
		muInProg:  sync.Mutex{},
		inProg:    make(map[pkg.Package]*upRec)}
	mon := NewSourceMonitor(d, r, f, CheckInterval)
	d.runCount.Add(1)
	for k := range s {
		d.addSource(s[k])
	}
	go func() {
		mon.Run()
		d.runCount.Done()
	}()
	d.srcMons = append(d.srcMons, mon)

	var srcs []source.Source
	var err error
	// Ignore if the directory doesn't exist
	if srcs, err = loadSourcesFromPath(store); err != nil && !os.IsNotExist(err) {
		return nil, err
	}

	for _, src := range srcs {
		if src.Enabled() {
			if err = d.addSource(src); err != nil {
				return nil, err
			}
		}
	}

	return d, nil
}

// loadSourcesFromPath loads sources from a directory, where each source gets
// it's own directory.  The actual directory structure is source dependent.
func loadSourcesFromPath(dir string) ([]source.Source, error) {
	files, err := ioutil.ReadDir(dir)
	if err != nil {
		return nil, err
	}

	srcs := make([]source.Source, 0, len(files))
	for _, f := range files {
		p := filepath.Join(dir, f.Name())
		src, err := source.LoadTUFSourceFromPath(p)
		if err != nil {
			return nil, err
		}
		srcs = append(srcs, src)
	}

	return srcs, nil
}

func (d *Daemon) addToActiveSrcs(s source.Source) {
	d.muSrcs.Lock()
	defer d.muSrcs.Unlock()

	id := s.GetId()

	if oldSource, ok := d.srcs[id]; ok {
		log.Printf("overwriting active source: %s", id)
		oldSource.Close()
	}

	d.srcs[id] = NewSourceKeeper(s)

	log.Printf("added source: %s", id)
}

// AddTUFSource is called to add a Source that can be used to get updates. When
// the TUFSource is added, the Daemon will start polling it at the interval
// from TUFSource.GetInterval()
func (d *Daemon) AddTUFSource(cfg *amber.SourceConfig) error {
	// Make sure the id is safe to be written to disk.
	store := filepath.Join(d.store, url.PathEscape(cfg.Id))

	src, err := source.NewTUFSource(store, cfg)
	if err != nil {
		log.Printf("failed to create TUF source: %v: %s", cfg.Id, err)
		return err
	}

	// Save the config.
	if err := src.Save(); err != nil {
		log.Printf("failed to save TUF config %v: %s", cfg.Id, err)
		return err
	}

	if !src.Enabled() {
		d.muSrcs.Lock()
		if _, ok := d.srcs[cfg.Id]; ok && cfg.BlobRepoUrl != "" {
			d.RemoveBlobRepo(cfg.BlobRepoUrl)
		}
		delete(d.srcs, cfg.Id)
		d.muSrcs.Unlock()
		return nil
	}

	return d.addSource(src)
}

func (d *Daemon) addSource(src source.Source) error {
	cfg := src.GetConfig()
	if cfg == nil {
		return fmt.Errorf("source does not have a config")
	}

	// Add the source's blob repo.
	blobRepo := BlobRepo{
		Source:   src,
		Address:  cfg.BlobRepoUrl,
		Interval: time.Second * 5,
	}

	if err := d.AddBlobRepo(blobRepo); err != nil {
		return err
	}

	// If we made it to this point, we're ready to actually add the source.
	d.addToActiveSrcs(src)

	// after the source is added, let the monitor(s) know so they can decide if they
	// want to look again for updates
	for _, m := range d.srcMons {
		m.SourcesAdded()
	}

	log.Printf("added TUF source %s %v\n", cfg.Id, cfg.RepoUrl)

	return nil
}

func (d *Daemon) RemoveTUFSource(id string) (amber.Status, error) {
	// If this method succeeds, the source should be removed from the
	// running service and not be loaded after a service restart. Delete
	// the config file before removing the source from the service to
	// ensure this behavior.
	s, err := d.removeTUFSource(id)
	if err != nil {
		return amber.StatusErr, nil
	} else if s == nil {
		return amber.StatusErrNotFound, nil
	}

	cfg := s.GetConfig()
	if cfg != nil {
		if found := d.RemoveBlobRepo(cfg.BlobRepoUrl); !found {
			log.Printf("blob repo not found: %s", cfg.BlobRepoUrl)
		}
	}

	s.Close()

	err = s.Delete()
	if err != nil {
		log.Printf("unable to remove TUFSource from disk: %v\n", err)
	}
	log.Printf("removed source: %s", id)

	return amber.StatusOk, nil
}

func (d *Daemon) removeTUFSource(id string) (source.Source, error) {
	d.muSrcs.Lock()
	defer d.muSrcs.Unlock()

	s, err := d.srcLock(id)
	if err != nil {
		log.Printf("source %q not found: %s", id, err)
		return nil, nil
	}

	err = s.DeleteConfig()
	if err != nil {
		log.Printf("unable to remove source config from disk: %v", err)
		return nil, err
	}

	delete(d.srcs, id)

	return s, nil
}

// srcLock gets a source, either an enabled or disabled one. It is the
// responsibility of the caller to hold muSrcs when calling.
func (d *Daemon) srcLock(srcID string) (source.Source, error) {
	src, ok := d.srcs[srcID]
	if ok {
		return src, nil
	}

	allSrcs, err := loadSourcesFromPath(d.store)
	if err != nil {
		return nil, err
	}

	for _, src := range allSrcs {
		if src.GetId() == srcID {
			return src, nil
		}
	}

	return nil, os.ErrNotExist
}

func (d *Daemon) Login(srcId string) (*amber.DeviceCode, error) {
	log.Printf("logging into %s", srcId)
	d.muSrcs.Lock()
	src, err := d.srcLock(srcId)
	d.muSrcs.Unlock()

	if err != nil {
		log.Printf("error getting source by ID: %s", err)
		return nil, fmt.Errorf("unknown source: %s", err)
	}

	return src.Login()
}

func (d *Daemon) blobRepos() []BlobRepo {
	d.muRepos.Lock()
	defer d.muRepos.Unlock()
	c := make([]BlobRepo, len(d.repos))
	copy(c, d.repos)
	return c
}

func (d *Daemon) AddBlobRepo(br BlobRepo) error {
	if _, err := url.ParseRequestURI(br.Address); err != nil {
		log.Printf("Provided URL %q is not valid", br.Address)
		return err
	}

	d.muRepos.Lock()
	d.repos = append(d.repos, br)
	d.muRepos.Unlock()

	log.Printf("added blob repo: %v\n", br.Address)

	return nil
}

// GetBlob is a blocking call which downloads the requested blob
func (d *Daemon) GetBlob(blob string) error {
	repos := d.blobRepos()
	return FetchBlob(repos, blob, DstBlob)
}

func (d *Daemon) RemoveBlobRepo(blobUrl string) bool {
	d.muRepos.Lock()
	defer d.muRepos.Unlock()

	for i := range d.repos {
		if d.repos[i].Address == blobUrl {
			d.repos = append(d.repos[:i], d.repos[i+1:]...)
			log.Printf("removed blob repo: %v\n", blobUrl)
			return true
		}
	}
	return false
}

type upRec struct {
	pkgs []*pkg.Package
	c    []chan<- map[pkg.Package]*GetResult
}

// GetResult represents the result of a package update request. To read the
// contents of the update call Open() and when done call Close(). These calls
// will Lock and Unlock to MuFile member to provide exclusive access to the
// underlying file.
type GetResult struct {
	*os.File
	Orig   pkg.Package
	Update pkg.Package
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
	pkgs map[pkg.Package]pkg.Package
	src  source.Source
}

func (d *Daemon) awaitResults(c chan map[pkg.Package]*GetResult, awaiting []*pkg.Package,
	out map[pkg.Package]*GetResult, mu *sync.Mutex) {
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
func (d *Daemon) GetUpdates(pkgs *pkg.PackageSet) map[pkg.Package]*GetResult {
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
	totalRes := make(map[pkg.Package]*GetResult)
	resLock := sync.Mutex{}
	// LOCK ACQUIRE
	d.muInProg.Lock()
	toGet := []*pkg.Package{}
	wg := &sync.WaitGroup{}

	// see which packages we might already be fetching and just subscribe
	// to that result if we are
	c := make(chan map[pkg.Package]*GetResult)
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

	// see if any requested packages are already on the system. We'll avoid
	// fetching these, but otherwise send through the rest of the process
	toGet = detectExisting(toGet, totalRes)

	var rec *upRec
	if len(toGet) > 0 {
		rec = &upRec{
			pkgs: toGet,
			c:    make([]chan<- map[pkg.Package]*GetResult, 0, 0),
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

// detectExisting checks to see if packages in the `pkgs` list already exist on
// the device. If they do exist an entry is inserted into resultSet with the
// Orig and Update set to the packge value in the `pkgs` list, with the Err
// value set to one that passes os.IsExist(). The list returned is any pkgs
// that do not exist already on the system. The expectation is that the
// synthesized GetResults will get passed through the rest of the pipeline
// and cause a 're-activation' of the installed package. This will cause
// the pipeline to 'repair' or complete the package if it is missing any
// contents.
func detectExisting(pkgs []*pkg.Package, resultSet map[pkg.Package]*GetResult) []*pkg.Package {
	//synthesize fetch results for existing packages

	retained := []*pkg.Package{}
	for _, pkg := range pkgs {
		if pkg.Merkle != "" {
			// try to create in the incoming package directory of pkgfs. If
			// this bounces back with ErrExist, the blob is already in blobfs
			// in some state. If we get any other error or no error at all,
			// try to install the package from scratch.
			f, err := os.OpenFile(filepath.Join(DstUpdate, pkg.Merkle), os.O_WRONLY|os.O_CREATE,
				os.ModePerm)
			if err != nil && os.IsExist(err) {
				resultSet[*pkg] = &GetResult{
					Orig:   *pkg,
					Update: *pkg,
					Err:    err,
				}
				continue
			} else if err == nil {
				// open succeeded, so clean up
				f.Close()
				// any error should be benign or result in an error down the line
				if rmErr := os.Remove(f.Name()); rmErr != nil {
					log.Printf("Unexpected error removing file: %s", rmErr)
				}
			}
		}
		retained = append(retained, pkg)
	}
	return retained
}

func (d *Daemon) getUpdates(rec *upRec) map[pkg.Package]*GetResult {
	fetchRecs := []*pkgSrcPair{}

	srcs := d.GetSources()

	unfoundPkgs := rec.pkgs
	// TODO thread-safe access for sources?
	for _, src := range srcs {
		if len(unfoundPkgs) == 0 {
			break
		}

		updates, err := src.AvailableUpdates(unfoundPkgs)
		if len(updates) == 0 || err != nil {
			if err == ErrRateExceeded {
				log.Printf("daemon: source rate limit exceeded")
			} else if err != nil {
				log.Printf("daemon: error checking source for updates %s", err)
			} else {
				log.Printf("daemon: no update found at source")
			}
			continue
		}

		pkgsToSrc := pkgSrcPair{
			pkgs: make(map[pkg.Package]pkg.Package),
			src:  src,
		}
		fetchRecs = append(fetchRecs, &pkgsToSrc)

		remaining := make([]*pkg.Package, 0, len(unfoundPkgs)-len(updates))
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
	results := make(map[pkg.Package]*GetResult)
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
		log.Println("daemon: While getting updates, the following packages were not found")
	}

	for _, pkg := range unfoundPkgs {
		res := GetResult{
			Orig: *pkg,
			File: nil,
			Err:  NewErrProcessPackage("update not found"),
		}
		log.Printf("daemon: %s\n", pkg)
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
func (d *Daemon) RemoveSource(src source.Source) error {
	// TODO(PKG-154) unify this method with RemoveTUFSource
	d.muSrcs.Lock()
	defer d.muSrcs.Unlock()

	id := src.GetId()

	if m, ok := d.srcs[id]; ok {
		if m.Equals(src) {
			delete(d.srcs, id)
			return nil
		}
	}

	return ErrSrcNotFound
}

func (d *Daemon) GetSources() map[string]source.Source {
	d.muSrcs.Lock()
	defer d.muSrcs.Unlock()

	srcs := make(map[string]source.Source)
	for key, value := range d.srcs {
		srcs[key] = value
	}

	// load any sources from disk that we may know about, but are not currently
	// using
	allSrcs, err := loadSourcesFromPath(d.store)
	if err != nil {
		log.Printf("couldn't load sources from disk: %s", err)
	} else {
		// don't override any in-memory entries
		for _, src := range allSrcs {
			if _, ok := srcs[src.GetId()]; !ok {
				srcs[src.GetId()] = src
			}
		}
	}

	return srcs
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

// ErrProcessPackage is a general I/O error during ProcessPackage
type ErrProcessPackage string

func NewErrProcessPackage(f string, args ...interface{}) error {
	return ErrProcessPackage(fmt.Sprintf("pkg processor: %s",
		fmt.Sprintf(f, args...)))
}

func (e ErrProcessPackage) Error() string {
	return string(e)
}

// ProcessPackage attempts to retrieve the content of the supplied Package
// from the supplied Source. If retrieval from the Source fails, the Source's
// error is returned. If there is a local I/O error when processing the package
// an ErrProcessPackage is returned.
func ProcessPackage(data *GetResult, pkgs *pkg.PackageSet) error {
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

	file, err := CreateOutputFile(data)
	if err != nil {
		return err
	}

	if _, err = WriteUpdateToPkgFS(data, file); err != nil {
		return err
	}

	pkgs.Replace(&data.Orig, &data.Update, false)
	return nil
}

// CreateOutputFile opens a path in pkgfs based on `data`'s merkle root. This
// function will return an error if the file already exists. Files which
// already exist will still cause pkgfs to import and/or heal the package that
// it represents, so the caller should consider whether an os.IsExist()-matching
// error should be treated as a failure.
func CreateOutputFile(data *GetResult) (*os.File, error) {
	dstPath := filepath.Join(DstUpdate, data.Update.Merkle)
	dst, e := os.OpenFile(dstPath, os.O_WRONLY|os.O_CREATE, os.ModePerm)
	if e != nil {
		return nil, e
	}
	return dst, nil
}

// WriteUpdateToPkgFS writes the data in the GetResult to `dst`. `dst` is
// closed before exiting.
func WriteUpdateToPkgFS(data *GetResult, dst *os.File) (string, error) {
	defer dst.Close()
	e := data.Open()
	if e != nil {
		return "", e
	}
	defer data.Close()

	i, err := data.Stat()
	if err != nil {
		return "", NewErrProcessPackage("couldn't stat temp file %s", e)
	}

	err = dst.Truncate(i.Size())
	if err != nil {
		return "", NewErrProcessPackage("couldn't truncate file destination %s", e)
	}
	written, err := io.Copy(dst, data)
	if err != nil {
		return "", NewErrProcessPackage("couldn't write update to file %s", err)
	}

	if written != i.Size() {
		return "", NewErrProcessPackage("pkg blob incomplete, only wrote %d out of %d bytes", written, i.Size())
	}

	return dst.Name(), nil
}

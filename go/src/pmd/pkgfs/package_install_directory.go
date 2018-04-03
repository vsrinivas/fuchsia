// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package pkgfs

import (
	"bytes"
	"encoding/json"
	"io"
	"io/ioutil"
	"log"
	"os"
	"path/filepath"
	"regexp"
	"strings"
	"time"

	"thinfs/fs"

	"fuchsia.googlesource.com/far"
	"fuchsia.googlesource.com/pm/pkg"
)

const (
	identityBlob = "15ec7bf0b50732b49f8228e07d24365338f9e3ab994b00af08e5a3bffe55fd8b"
)

var merklePat = regexp.MustCompile("^[0-9a-f]{64}$")

/*
The following VNodes are defined in this file:

/install           - installDir
/install/pkg       - installPkgDir
/install/pkg/{f}   - installFile{isPkg:true}
/install/blob      - installBlobDir
/install/blob/{f}  - installFile{isPkg:false}
*/

// installDir is located at /install.
type installDir struct {
	unsupportedDirectory

	fs *Filesystem
}

func (d *installDir) Dup() (fs.Directory, error) {
	return d, nil
}

func (d *installDir) Stat() (int64, time.Time, time.Time, error) {
	return 0, d.fs.mountTime, d.fs.mountTime, nil
}

func (d *installDir) Open(name string, flags fs.OpenFlags) (fs.File, fs.Directory, *fs.Remote, error) {
	name = clean(name)
	if name == "" {
		return nil, d, nil, nil
	}

	parts := strings.SplitN(name, "/", 2)

	var nd fs.Directory
	switch parts[0] {
	case "pkg":
		nd = &installPkgDir{fs: d.fs}
	case "blob":
		nd = &installBlobDir{fs: d.fs}
	default:
		return nil, nil, nil, fs.ErrNotFound
	}

	if len(parts) == 1 {
		if flags.Directory() {
			return nil, nd, nil, nil
		}
		return nil, nd, nil, fs.ErrNotSupported
	}

	return nd.Open(parts[1], flags)
}

func (d *installDir) Read() ([]fs.Dirent, error) {
	return []fs.Dirent{dirDirEnt("pkg"), dirDirEnt("blob")}, nil
}

func (d *installDir) Close() error {
	return nil
}

// installPkgDir is located at /install/pkg
type installPkgDir struct {
	unsupportedDirectory

	fs *Filesystem
}

func (d *installPkgDir) Dup() (fs.Directory, error) {
	return d, nil
}

func (d *installPkgDir) Stat() (int64, time.Time, time.Time, error) {
	return 0, d.fs.mountTime, d.fs.mountTime, nil
}

func (d *installPkgDir) Open(name string, flags fs.OpenFlags) (fs.File, fs.Directory, *fs.Remote, error) {
	name = clean(name)
	if name == "" {
		return nil, d, nil, nil
	}

	if !flags.Create() {
		return nil, nil, nil, fs.ErrNotSupported
	}

	f := &installFile{fs: d.fs, name: name, isPkg: true}
	err := f.open()
	return f, nil, nil, err
}

func (d *installPkgDir) Read() ([]fs.Dirent, error) {
	return []fs.Dirent{}, nil
}

func (d *installPkgDir) Close() error {
	return nil
}

// installBlobDir is located at /install/blob
type installBlobDir struct {
	unsupportedDirectory

	fs *Filesystem
}

func (d *installBlobDir) Dup() (fs.Directory, error) {
	return d, nil
}

func (d *installBlobDir) Stat() (int64, time.Time, time.Time, error) {
	return 0, d.fs.mountTime, d.fs.mountTime, nil
}

func (d *installBlobDir) Open(name string, flags fs.OpenFlags) (fs.File, fs.Directory, *fs.Remote, error) {
	name = clean(name)
	if name == "" {
		return nil, d, nil, nil
	}

	// TODO(raggi): support write resumption..

	if !flags.Create() {
		return nil, nil, nil, fs.ErrNotSupported
	}

	f := &installFile{fs: d.fs, name: name, isPkg: false}
	err := f.open()
	return f, nil, nil, err
}

func (d *installBlobDir) Read() ([]fs.Dirent, error) {
	return []fs.Dirent{}, nil
}

func (d *installBlobDir) Close() error {
	return nil
}

type installFile struct {
	unsupportedFile
	fs *Filesystem

	isPkg bool

	size    uint64
	written uint64
	name    string

	blob *os.File
}

func (f *installFile) open() error {
	if !merklePat.Match([]byte(f.name)) {
		return fs.ErrInvalidArgs
	}

	if f.fs.blobfs.HasBlob(f.name) {
		return fs.ErrAlreadyExists
	}

	var err error
	f.blob, err = os.Create(f.fs.blobfs.PathOf(f.name))
	if err != nil {
		return goErrToFSErr(err)
	}
	return nil
}

func (f *installFile) Write(p []byte, off int64, whence int) (int, error) {
	if whence != fs.WhenceFromCurrent || off != 0 {
		return 0, fs.ErrNotSupported
	}

	n, err := f.blob.Write(p)

	f.written += uint64(n)

	if f.written >= f.size && err == nil {
		if f.isPkg {
			// TODO(raggi): use already open file instead of re-opening the file
			importPackage(f.fs, f.name)
		}

		// TODO(raggi): check which of these really needs to be done, and/or move them into checkNeeds:
		os.Remove(f.fs.index.NeedsBlob(f.name))
		os.Remove(f.fs.index.NeedsFile(f.name))

		checkNeeds(f.fs, f.name)
	}

	return n, goErrToFSErr(err)
}

func (f *installFile) Close() error {
	return goErrToFSErr(f.blob.Close())
}

func (f *installFile) Stat() (int64, time.Time, time.Time, error) {
	return int64(f.written), time.Time{}, time.Time{}, nil
}

func (f *installFile) Truncate(sz uint64) error {
	f.size = sz
	err := f.blob.Truncate(int64(f.size))

	if f.size == 0 && f.name == identityBlob && err == nil {
		// TODO(raggi): check which of these really needs to be done, and/or move them into checkNeeds:
		os.Remove(f.fs.index.NeedsBlob(f.name))
		os.Remove(f.fs.index.NeedsFile(f.name))
		checkNeeds(f.fs, f.name)
	}

	return goErrToFSErr(err)
}

// importPackage reads a package far from blobfs, given a content key, and imports it into the package index
func importPackage(fs *Filesystem, root string) {
	log.Printf("pkgfs: importing package from %q", root)

	f, err := fs.blobfs.Open(root)
	if err != nil {
		log.Printf("error importing package: %s", err)
		return
	}
	defer f.Close()

	// TODO(raggi): this is a bit messy, the system could instead force people to
	// write to specific paths in the incoming directory
	if !far.IsFAR(f) {
		log.Printf("pkgfs:importPackage: %q is not a package, ignoring import", root)
		return
	}
	f.Seek(0, io.SeekStart)

	r, err := far.NewReader(f)
	if err != nil {
		log.Printf("error reading package archive package: %s", err)
		return
	}

	// TODO(raggi): this can also be replaced if we enforce writes into specific places in the incoming tree
	var isPkg bool
	for _, f := range r.List() {
		if strings.HasPrefix(f, "meta/") {
			isPkg = true
		}
	}
	if !isPkg {
		log.Printf("pkgfs: %q does not contain a meta directory, assuming it is not a package", root)
		return
	}

	pf, err := r.ReadFile("meta/package")
	if err != nil {
		log.Printf("error reading package metadata: %s", err)
		return
	}

	var p pkg.Package
	err = json.Unmarshal(pf, &p)
	if err != nil {
		log.Printf("error parsing package metadata: %s", err)
		return
	}

	if err := p.Validate(); err != nil {
		log.Printf("pkgfs: package is invalid: %s", err)
		return
	}

	contents, err := r.ReadFile("meta/contents")
	if err != nil {
		log.Printf("pkgfs: error parsing package contents file for %s: %s", p, err)
		return
	}

	pkgInstalling := fs.index.InstallingPackageVersionPath(p.Name, p.Version)
	os.MkdirAll(filepath.Dir(pkgInstalling), os.ModePerm)
	if err := ioutil.WriteFile(pkgInstalling, []byte(root), os.ModePerm); err != nil {
		log.Printf("error writing package installing index for %s: %s", p, err)
	}
	pkgWaitingDir := fs.index.WaitingPackageVersionPath(p.Name, p.Version)
	os.MkdirAll(pkgWaitingDir, os.ModePerm)

	files := bytes.Split(contents, []byte{'\n'})
	var needsCount int
	var needBlobs []string

	for i := range files {
		parts := bytes.SplitN(files[i], []byte{'='}, 2)
		if len(parts) != 2 {
			// TODO(raggi): log illegal contents format?
			continue
		}
		root := string(parts[1])

		if fs.blobfs.HasBlob(root) {
			log.Printf("pkgfs: blob already present for %s: %q", p, root)
			continue
		}

		needsCount++

		err = ioutil.WriteFile(filepath.Join(pkgWaitingDir, root), []byte{}, os.ModePerm)
		if err != nil {
			log.Printf("pkgfs: import error, can't create waiting index for %s: %s", p, err)
		}

		err = ioutil.WriteFile(fs.index.NeedsBlob(root), []byte{}, os.ModePerm)
		if err != nil {
			// XXX(raggi): there are potential deadlock conditions here, we should fail the package write (???)
			log.Printf("pkgfs: import error, can't create needs index for %s: %s", p, err)
		}

		needBlobs = append(needBlobs, root)
	}

	if needsCount == 0 {
		activatePackage(p, fs)
	}

	go func() {
		for _, root := range needBlobs {
			log.Printf("pkgfs: asking amber to fetch blob for %s: %q", p, root)
			// TODO(jmatt) limit concurrency, send this to a worker routine?
			fs.amberPxy.GetBlob(root)
		}
	}()

	checkNeeds(fs, root)
}

func checkNeeds(fs *Filesystem, root string) {
	fulfillments, err := filepath.Glob(filepath.Join(fs.index.WaitingPackageVersionPath("*", "*"), root))
	if err != nil {
		log.Printf("pkgfs: error checking fulfillment of %s: %s", root, err)
		return
	}
	for _, path := range fulfillments {
		if err := os.Remove(path); err != nil {
			log.Printf("pkgfs: error removing %q: %s", path, err)
		}

		pkgWaitingDir := filepath.Dir(path)

		dir, err := os.Open(pkgWaitingDir)
		if err != nil {
			log.Printf("pkgfs: error opening waiting dir: %s: %s", pkgWaitingDir, err)
			continue
		}
		names, err := dir.Readdirnames(0)
		dir.Close()
		if err != nil {
			log.Printf("pkgfs: failed to check waiting dir %s: %s", pkgWaitingDir, err)
			continue
		}
		// if all the needs are fulfilled, move the package from installing to packages.
		if len(names) == 0 {
			pkgNameVersion, err := filepath.Rel(fs.index.WaitingDir(), pkgWaitingDir)
			if err != nil {
				log.Printf("pkgfs: error extracting package name from %s: %s", pkgWaitingDir, err)
				continue
			}

			parts := strings.SplitN(pkgNameVersion, "/", 2)
			p := pkg.Package{Name: parts[0], Version: parts[1]}

			activatePackage(p, fs)

		}
	}
}

func activatePackage(p pkg.Package, fs *Filesystem) {
	log.Printf("pkgfs: activating %s", p)
	from := filepath.Join(fs.index.InstallingDir(), p.Name, p.Version)
	b, err := ioutil.ReadFile(from)
	if err != nil {
		log.Printf("pkgfs: error reading package installing manifest for %s: %s", p, err)
		return
	}
	root := string(b)
	if _, ok := fs.static.Get(p); ok {
		fs.static.Set(p, root)
		debugLog("package %s ready, updated static index", p)
		os.Remove(from)
	} else {
		to := filepath.Join(fs.index.PackagesDir(), p.Name, p.Version)
		os.MkdirAll(filepath.Dir(to), os.ModePerm)
		debugLog("package %s ready, moving %s to %s", p, from, to)
		if err := os.Rename(from, to); err != nil {
			// TODO(raggi): this kind of state will need to be cleaned up by a general garbage collector at a later time.
			log.Printf("pkgfs: error moving package from installing to packages: %s", err)
		}
	}
	os.Remove(filepath.Join(fs.index.WaitingPackageVersionPath(p.Name, p.Version)))
}

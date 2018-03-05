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
	"strings"
	"sync"
	"thinfs/fs"
	"time"

	"fuchsia.googlesource.com/far"
	"fuchsia.googlesource.com/pm/pkg"
	"fuchsia.googlesource.com/pmd/blobfs"
)

// inDirectory is located at /in. It accepts newly created files, and writes them to blobfs.
type inDirectory struct {
	unsupportedDirectory

	fs *Filesystem
}

func (d *inDirectory) Dup() (fs.Directory, error) {
	return d, nil
}

func (d *inDirectory) Stat() (int64, time.Time, time.Time, error) {
	debugLog("pkgfs:in:stat")
	return 0, d.fs.mountTime, d.fs.mountTime, nil
}

func (d *inDirectory) Open(name string, flags fs.OpenFlags) (fs.File, fs.Directory, *fs.Remote, error) {
	name = clean(name)
	debugLog("pkgfs:in:open %q", name)
	if name == "" {
		return nil, d, nil, nil
	}

	if !(flags.Create() && flags.File()) {
		return nil, nil, nil, fs.ErrNotFound
	}
	// TODO(raggi): validate/reject other flags

	// TODO(raggi): create separate incoming directories for blobs and packages
	return &inFile{unsupportedFile: unsupportedFile("/pkgfs/incoming/" + name), fs: d.fs, oname: name, name: ""}, nil, nil, nil
}

func (d *inDirectory) Close() error {
	debugLog("pkgfs:in:close")
	return nil
}

func (d *inDirectory) Unlink(target string) error {
	debugLog("pkgfs:in:unlink %s", target)
	return fs.ErrNotFound
}

type inFile struct {
	unsupportedFile
	fs *Filesystem

	mu sync.Mutex

	oname string
	name  string
	sz    int64
	w     io.WriteCloser
}

func (f *inFile) Write(p []byte, off int64, whence int) (int, error) {
	debugLog("pkgfs:infile:write %q [%d @ %d]", f.oname, len(p), off)
	f.mu.Lock()
	defer f.mu.Unlock()

	if f.w == nil {
		var err error
		f.w, err = f.fs.blobfs.Create(f.name, f.sz)
		if err != nil {
			log.Printf("pkgfs: incoming file %q blobfs creation failed %q: %s", f.oname, f.name, err)
			return 0, goErrToFSErr(err)
		}
	}

	if whence != fs.WhenceFromCurrent || off != 0 {
		return 0, fs.ErrNotSupported
	}

	n, err := f.w.Write(p)
	return n, goErrToFSErr(err)
}

func (f *inFile) Close() error {
	debugLog("pkgfs:infile:close %q", f.oname)
	f.mu.Lock()
	defer f.mu.Unlock()

	if f.w == nil {
		var err error
		f.w, err = f.fs.blobfs.Create(f.name, f.sz)
		if err != nil {
			log.Printf("pkgfs: incoming file %q blobfs creation failed %q: %s", f.oname, f.name, err)
			return goErrToFSErr(err)
		}
	}

	err := f.w.Close()

	root := f.name
	if rooter, ok := f.w.(blobfs.Rooter); ok {
		root, err = rooter.Root()
		if err != nil {
			log.Printf("pkgfs: root digest computation failed after successful blobfs write: %s", err)
			return goErrToFSErr(err)
		}
	}

	if err == nil || f.fs.blobfs.HasBlob(root) {
		log.Printf("pkgfs: wrote %q to blob %q", f.oname, root)

		if f.oname == root {
			// TODO(raggi): removing the blob need after the blob is written should signal to package activation that the need is fulfilled.
			os.Remove(f.fs.index.NeedsBlob(root))

			// XXX(raggi): TODO(raggi): temporary hack for synchronous needs fulfillment:
			checkNeeds(f.fs, root)
		} else {
			// remove the needs declaration that has just been fulfilled
			os.Remove(f.fs.index.NeedsFile(f.oname))

			// TODO(raggi): make this asynchronous? (makes the tests slightly harder)
			importPackage(f.fs, root)
		}
	}
	if err != nil {
		log.Printf("pkgfs: failed incoming file write to blobfs: %s", err)
	}
	return goErrToFSErr(err)
}

func (f *inFile) Stat() (int64, time.Time, time.Time, error) {
	debugLog("pkgfs:infile:stat %q", f.oname)
	return 0, time.Time{}, time.Time{}, nil
}

func (f *inFile) Truncate(sz uint64) error {
	debugLog("pkgfs:infile:truncate %q", f.oname)
	if f.w != nil {
		return fs.ErrInvalidArgs
	}
	// XXX(raggi): the usual Go mess with io size operations, this truncation is potentially bad.
	f.sz = int64(sz)
	return nil
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

		log.Printf("pkgfs: asking amber to fetch blob for %s: %q", p, root)
		// TODO(jmatt) limit concurrency, send this to a worker routine?
		go fs.amberPxy.GetBlob(root)
	}

	if needsCount == 0 {
		activatePackage(p, fs)
	}

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

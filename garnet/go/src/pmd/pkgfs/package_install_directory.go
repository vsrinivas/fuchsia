// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package pkgfs

import (
	"bytes"
	"encoding/json"
	"log"
	"os"
	"regexp"
	"strings"
	"syscall/zx"
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
	var err error
	if !flags.Path() {
		err = f.open()
	}
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

	var err error

	// TODO(raggi): propagate flags instead to allow for resumption and so on
	f.blob, err = f.fs.blobfs.OpenFile(f.name, os.O_WRONLY|os.O_CREATE, 0777)

	// When opening a blob for write, blobfs returns a permission error if
	// the blob is in the process of being written or already exists. If we
	// can confirm the blob is readable, report to the caller that it
	// already exists. Otherwise, bubble the error to the caller without
	// fulfilling the need.
	if os.IsPermission(err) {
		if !f.fs.blobfs.HasBlob(f.name) {
			return goErrToFSErr(err)
		}

		// "Fulfill" any needs against the blob that was attempted to be written.
		f.fs.index.Fulfill(f.name)

		if f.isPkg {
			// Importing a package file that is already present in blobfs could fail for
			// a number of reasons, such as the package being invalid, and so on. We need
			// to report such cases back to the caller. In the case where we import fine,
			// we must fall through to passing ErrAlreadyExists back to the caller, so
			// that they know that the package file itself is already complete. Getting
			// `fs.ErrAlreadyExists` on a package meta.far write does not in and of
			// itself indicate that the whole package is present, only that the package
			// metadata blob is present.
			if err := f.importPackage(); err != nil {
				return err
			}
		}
		return fs.ErrAlreadyExists
	}

	if err != nil {
		return goErrToFSErr(err)
	}

	if f.isPkg {
		f.fs.index.Installing(f.name)
	}
	return nil
}

func (f *installFile) Write(p []byte, off int64, whence int) (int, error) {
	var err error
	n := 0

	if whence != fs.WhenceFromCurrent || off != 0 {
		err = &zx.Error{Status: zx.ErrNotSupported}
		return n, goErrToFSErr(err)
	}

	// It is illegal to write past the truncated size of a blob.
	if f.written > f.size {
		err = &zx.Error{Status: zx.ErrInvalidArgs}
		return n, goErrToFSErr(err)
	}

	n, err = f.blob.Write(p)
	f.written += uint64(n)

	if f.written >= f.size && err == nil {
		// "Fulfill" any needs against the blob that was attempted to be written.
		f.fs.index.Fulfill(f.name)

		if f.isPkg {
			// If a package installation fails, the error is returned here.
			return n, goErrToFSErr(f.importPackage())
		}
	}

	return n, goErrToFSErr(err)
}

func (f *installFile) Close() error {
	if f.blob == nil {
		return nil
	}

	if err := f.blob.Close(); err != nil {
		log.Printf("error closing file: %s\n", err)
		return goErrToFSErr(err)
	}

	return nil
}

func (f *installFile) Stat() (int64, time.Time, time.Time, error) {
	return int64(f.written), time.Time{}, time.Time{}, nil
}

func (f *installFile) Truncate(sz uint64) error {
	var err error

	f.size = sz
	err = f.blob.Truncate(int64(f.size))

	if f.size == 0 && f.name == identityBlob && err == nil {
		// Fulfill any needs against the identity blob
		f.fs.index.Fulfill(f.name)
	}

	return goErrToFSErr(err)
}

// importPackage uses f.name to import the package that was just written. It
// returns an fs.Error to report back to the user at write time.
func (f *installFile) importPackage() error {
	return importPackage(f.fs, f.name)
}

// importPackage uses name to import the package that was just written. It
// returns an fs.Error to report back to the user at write time.
func importPackage(f *Filesystem, name string) error {
	b, err := f.blobfs.Open(name)
	if err != nil {
		f.index.InstallingFailedForPackage(name)
		log.Printf("error opening package blob after writing: %s: %s", name, err)
		return fs.ErrFailedPrecondition
	}
	defer b.Close()

	r, err := far.NewReader(b)
	if err != nil {
		f.index.InstallingFailedForPackage(name)
		log.Printf("error reading package archive: %s", err)
		// Note: translates to zx.ErrBadState
		return fs.ErrFailedPrecondition
	}

	pf, err := r.ReadFile("meta/package")
	if err != nil {
		f.index.InstallingFailedForPackage(name)
		log.Printf("error reading package metadata: %s", err)
		// Note: translates to zx.ErrBadState
		return fs.ErrFailedPrecondition
	}

	var p pkg.Package
	err = json.Unmarshal(pf, &p)
	if err != nil {
		f.index.InstallingFailedForPackage(name)
		log.Printf("error parsing package metadata: %s", err)
		// Note: translates to zx.ErrBadState
		return fs.ErrFailedPrecondition
	}

	if err := p.Validate(); err != nil {
		f.index.InstallingFailedForPackage(name)
		log.Printf("package is invalid: %s", err)
		// Note: translates to zx.ErrBadState
		return fs.ErrFailedPrecondition
	}

	// Tell the index the identity of this package.
	f.index.UpdateInstalling(name, p)

	contents, err := r.ReadFile("meta/contents")
	if err != nil {
		log.Printf("error parsing package contents file for %s: %s", p, err)
		// Note: translates to zx.ErrBadState
		return fs.ErrFailedPrecondition
	}

	files := bytes.Split(contents, []byte{'\n'})

	// Note: the following heuristic is coarse and not easy to compute. For small
	// packages enumerating all blobs in blobfs will be slow, but for very large
	// packages it's more likely that polling blobfs the expensive way for missing
	// blobs is going to be expensive. This can be improved by blobfs providing an
	// API to handle this case of "which of the following blobs are already
	// readable"
	mayHaveBlob := func(root string) bool { return true }
	if len(files) > 20 {
		dnames, err := f.blobfs.Blobs()
		if err != nil {
			log.Printf("error readdir blobfs: %s", err)
			// Note: translates to zx.ErrBadState
			return fs.ErrFailedPrecondition
		}
		names := map[string]struct{}{}
		for _, name := range dnames {
			names[name] = struct{}{}
		}
		mayHaveBlob = func(root string) bool {
			_, found := names[root]
			return found
		}
	}

	needBlobs := make(map[string]struct{})
	foundBlobs := make(map[string]struct{})
	needsCount := 0
	for i := range files {
		// Silence apparent errors from last line in file/empty lines.
		if len(files[i]) == 0 {
			continue
		}
		parts := bytes.SplitN(files[i], []byte{'='}, 2)
		if len(parts) != 2 {
			log.Printf("skipping bad package entry: %q", files[i])
			continue
		}
		root := string(parts[1])

		if mayHaveBlob(root) && f.blobfs.HasBlob(root) {
			foundBlobs[root] = struct{}{}
			continue
		}

		needsCount++
		needBlobs[root] = struct{}{}
	}

	// NOTE: the EEXIST returned here is sometimes not strictly "this package was
	// already activated", as the package may have just been activated during the
	// above loop that calls fulfill with each blob that is found that already
	// exists. Doing otherwise would significantly harm performance, as it would
	// require a strong consistency rather than an eventual consistency model, that
	// requires global locking of the filesystem. What this means in the not
	// entirely correct case is either:
	// a) we raced another process that was fulfilling the same needs as this
	//    package.
	// b) all of the content was already on the system, but the package was missing
	//    from the index.
	// This state could be improved if there was an in-memory precomputed index of
	// all active meta.far blobs on the system.
	if needsCount == 0 {
		// It is possible that we already had all of the content for a package at the
		// time when importPackage starts, for example if a package is updated and
		// then reverted to a prior version wihtout GC. In that case, we should still
		// activate the package, even though there is nothing to fulfill.
		f.index.Add(p, name)
		return fs.ErrAlreadyExists
	}

	// We tell the index about needs that we have which were explicitly not found
	// on the system.
	err = goErrToFSErr(f.index.AddNeeds(name, needBlobs))

	// In order to ensure eventual consistency in the case where multiple processes
	// are racing on these needs, we must re-publish all of those fulfillments
	// after publishing the locally discovered needs.
	for blob := range foundBlobs {
		f.index.Fulfill(blob)
	}

	// AddNeeds may return os.ErrExist if the package activation won the race
	// between our needs check loop above, and the following registration of the
	// packages needs.
	return err
}

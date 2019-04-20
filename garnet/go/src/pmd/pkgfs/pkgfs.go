// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Package pkgfs hosts a filesystem for interacting with packages that are
// stored on a host. It presents a tree of packages that are locally available
// and a tree that enables a user to add new packages and/or package content to
// the host.
package pkgfs

import (
	"fmt"
	"io"
	"log"
	"os"
	"path"
	"path/filepath"
	"runtime"
	"sync"
	"syscall"
	"syscall/zx"
	"syscall/zx/fidl"
	zxio "syscall/zx/io"
	"time"

	"thinfs/fs"
	"thinfs/zircon/rpc"

	"fuchsia.googlesource.com/pm/pkg"
	"fuchsia.googlesource.com/pmd/blobfs"
	"fuchsia.googlesource.com/pmd/index"
)

// Filesystem is the top level container for a pkgfs server
type Filesystem struct {
	root      *rootDirectory
	static    *index.StaticIndex
	index     *index.DynamicIndex
	blobfs    *blobfs.Manager
	mountInfo mountInfo
	mountTime time.Time
}

// New initializes a new pkgfs filesystem server
func New(indexDir string, blobDir string) (*Filesystem, error) {
	bm, err := blobfs.New(blobDir)
	if err != nil {
		return nil, fmt.Errorf("pkgfs: open blobfs: %s", err)
	}

	static := index.NewStatic()
	f := &Filesystem{
		static: static,
		index:  index.NewDynamic(indexDir, static),
		blobfs: bm,
		mountInfo: mountInfo{
			parentFd: -1,
		},
	}

	f.root = &rootDirectory{
		unsupportedDirectory: unsupportedDirectory("/"),
		fs:                   f,

		dirs: map[string]fs.Directory{
			"install": &installDir{
				unsupportedDirectory: unsupportedDirectory("/install"),
				fs:                   f,
			},
			"needs": &needsRoot{
				unsupportedDirectory: unsupportedDirectory("/needs"),
				fs:                   f,
			},
			"garbage": unsupportedDirectory("/garbage"),
			"packages": &packagesRoot{
				unsupportedDirectory: unsupportedDirectory("/packages"),
				fs:                   f,
			},
			"versions": &versionsDirectory{
				unsupportedDirectory: unsupportedDirectory("/versions"),
				fs:                   f,
			},
			"system": unsupportedDirectory("/system"),
			"validation": &validationDir{unsupportedDirectory: unsupportedDirectory("/validation"),
				fs: f,
			},
		},
	}

	return f, nil
}

// staticIndexPath is the path inside the system package directory that contains the static packages for that system version.
const staticIndexPath = "data/static_packages"

// loadStaticIndex loads the blob specified by root from blobfs. A non-nil
// *StaticIndex is always returned. If an error is returned that indicates a
// problem reading the index content from disk and therefore the StaticIndex
// returned may be empty.
func loadStaticIndex(static *index.StaticIndex, blobfs *blobfs.Manager, root string) error {
	indexFile, err := blobfs.Open(root)
	if err != nil {
		return fmt.Errorf("pkgfs: could not load static index from blob %s: %s", root, err)
	}
	defer indexFile.Close()

	return static.LoadFrom(indexFile)
}

func readBlobfs(blobfs *blobfs.Manager) (map[string]struct{}, error) {
	dnames, err := readDir(blobfs.Root)
	if err != nil {
		log.Printf("pkgfs: error reading(%q): %s", blobfs.Root, err)
		// Note: translates to zx.ErrBadState
		return nil, fs.ErrFailedPrecondition
	}
	names := make(map[string]struct{})
	for _, name := range dnames {
		names[name] = struct{}{}
	}
	return names, nil
}

func readDir(path string) ([]string, error) {
	d, err := os.Open(path)
	if err != nil {
		return nil, err
	}
	defer d.Close()
	return d.Readdirnames(-1)
}

// SetSystemRoot sets/updates the merkleroot (and static index) that backs the /system partition and static package index.
func (f *Filesystem) SetSystemRoot(merkleroot string) error {
	pd, err := newPackageDirFromBlob(merkleroot, f)
	if err != nil {
		return err
	}
	f.root.setDir("system", pd)

	blob, ok := pd.getBlobFor(staticIndexPath)
	if !ok {
		return fmt.Errorf("pkgfs: new system root set, but new static index %q not found in %q", staticIndexPath, merkleroot)
	}

	err = loadStaticIndex(f.static, f.blobfs, blob)

	// Ensure that the "system_image" package is also indexed
	f.static.Set(
		pkg.Package{
			Name:    pd.name,
			Version: pd.version,
		},
		merkleroot,
	)

	return err
}

func (f *Filesystem) Blockcount() int64 {
	// TODO(raggi): sum up all packages?
	// TODO(raggi): delegate to blobfs?
	debugLog("fs blockcount")
	return 0
}

func (f *Filesystem) Blocksize() int64 {
	// TODO(raggi): sum up all packages?
	// TODO(raggi): delegate to blobfs?
	debugLog("fs blocksize")
	return 0
}

func (f *Filesystem) Size() int64 {
	debugLog("fs size")
	// TODO(raggi): delegate to blobfs?
	return 0
}

func (f *Filesystem) Close() error {
	f.Unmount()
	return nil
}

func (f *Filesystem) RootDirectory() fs.Directory {
	return f.root
}

func (f *Filesystem) Type() string {
	return "pkgfs"
}

func (f *Filesystem) FreeSize() int64 {
	return 0
}

func (f *Filesystem) DevicePath() string {
	return ""
}

// Serve starts a Directory protocol RPC server on the given channel.
func (f *Filesystem) Serve(c zx.Channel) error {
	// rpc.NewServer takes ownership of the Handle and will close it on error.
	vfs, err := rpc.NewServer(f, zx.Handle(c))
	if err != nil {
		return fmt.Errorf("vfs server creation: %s", err)
	}
	f.mountInfo.serveChannel = c

	// TODO(raggi): serve has no quit/shutdown path.
	for i := runtime.NumCPU(); i > 1; i-- {
		go vfs.Serve()
	}
	vfs.Serve()
	return nil
}

// Mount attaches the filesystem host to the given path. If an error occurs
// during setup, this method returns that error. If an error occurs after
// serving has started, the error causes a log.Fatal. If the given path does not
// exist, it is created before mounting.
func (f *Filesystem) Mount(path string) error {
	err := os.MkdirAll(path, os.ModePerm)
	if err != nil {
		return err
	}

	f.mountInfo.parentFd, err = syscall.Open(path, syscall.O_ADMIN|syscall.O_DIRECTORY, 0777)
	if err != nil {
		return err
	}

	var rpcChan, mountChan zx.Channel
	rpcChan, mountChan, err = zx.NewChannel(0)
	if err != nil {
		syscall.Close(f.mountInfo.parentFd)
		f.mountInfo.parentFd = -1
		return fmt.Errorf("channel creation: %s", err)
	}

	remote := zxio.DirectoryInterface(fidl.InterfaceRequest{Channel: mountChan})
	dirChan := zx.Channel(syscall.FDIOForFD(f.mountInfo.parentFd).Handles()[0])
	dir := zxio.DirectoryAdminInterface(fidl.InterfaceRequest{Channel: dirChan})
	if status, err := dir.Mount(remote); err != nil || zx.Status(status) != zx.ErrOk {
		rpcChan.Close()
		syscall.Close(f.mountInfo.parentFd)
		f.mountInfo.parentFd = -1
		return fmt.Errorf("mount failure: %s", err)
	}

	return f.Serve(rpcChan)
}

// Unmount detaches the filesystem from a previously mounted path. If mount was not previously called or successful, this will panic.
func (f *Filesystem) Unmount() {
	f.mountInfo.unmountOnce.Do(func() {
		// parentFd is -1 in the case where f was just Serve()'d instead of Mount()'d
		if f.mountInfo.parentFd != -1 {
			dirChan := zx.Channel(syscall.FDIOForFD(f.mountInfo.parentFd).Handles()[0])
			dir := zxio.DirectoryAdminInterface(fidl.InterfaceRequest{Channel: dirChan})
			dir.UnmountNode()
			syscall.Close(f.mountInfo.parentFd)
			f.mountInfo.parentFd = -1
		}
		f.mountInfo.serveChannel.Close()
		f.mountInfo.serveChannel = 0
	})
}

var _ fs.FileSystem = (*Filesystem)(nil)

// clean canonicalizes a path and returns a path that is relative to an assumed root.
// as a result of this cleaning operation, an open of '/' or '.' or '' all return ''.
// TODO(raggi): speed this up/reduce allocation overhead.
func clean(path string) string {
	return filepath.Clean("/" + path)[1:]
}

type mountInfo struct {
	unmountOnce  sync.Once
	serveChannel zx.Channel
	parentFd     int
}

func goErrToFSErr(err error) error {
	switch e := err.(type) {
	case nil:
		return nil
	case *os.PathError:
		return goErrToFSErr(e.Err)
	case *zx.Error:
		switch e.Status {
		case zx.ErrNotFound:
			return fs.ErrNotFound
		case zx.ErrNoSpace:
			return fs.ErrNoSpace
		case zx.ErrNotSupported:
			return fs.ErrNotSupported
		case zx.ErrInvalidArgs:
			return fs.ErrInvalidArgs
		default:
			debugLog("pkgfs: unmapped zx status to fs err: %s", e.Status)
			return err
		}
	}
	switch err {
	case os.ErrInvalid:
		return fs.ErrInvalidArgs
	case os.ErrPermission:
		return fs.ErrPermission
	case os.ErrExist:
		return fs.ErrAlreadyExists
	case os.ErrNotExist:
		return fs.ErrNotFound
	case os.ErrClosed:
		return fs.ErrNotOpen
	case io.EOF:
		return fs.ErrEOF
	default:
		debugLog("pkgfs: unmapped os err to fs err: %T %v", err, err)
		return err
	}
}

// GC examines the static and dynamic indexes, collects all the blobs that
// belong to packages in these indexes. It then reads blobfs for its entire
// list of blobs. Anything in blobfs that does not appear in the indexes is
// removed.
func (fs *Filesystem) GC() error {
	log.Println("GC: start")
	start := time.Now()
	defer func() {
		// this process produces a lot of garbage, so try to free that up (removes
		// ~1.5mb of heap from a common (small) build target).
		runtime.GC()
		log.Printf("GC: completed in %.3fs", time.Since(start).Seconds())
	}()

	// read the list of installed blobs first, as there may be installations in
	// progress, we want to not create orphans later, this list must be equal to or
	// a subset of the set we intersect.
	installedBlobs, err := readBlobfs(fs.blobfs)
	if err != nil {
		return fmt.Errorf("GC: unable to list blobfs: %s", err)
	}
	log.Printf("GC: %d blobs in blobfs", len(installedBlobs))

	allPackageBlobs := fs.index.AllPackageBlobs()
	// access the meta FAR blob of the system package
	if pd, ok := fs.root.dir("system").(*packageDir); ok {
		allPackageBlobs = append(allPackageBlobs, pd.contents["meta"])
	} else {
		return fmt.Errorf("GC: gc aborted, system directory is of unknown type")
	}

	// Walk the list of all packages and collate all involved blobs, both the fars
	// themselves, and the contents on which they depend.
	allBlobs := make(map[string]struct{})
	for _, pkgRoot := range allPackageBlobs {
		allBlobs[pkgRoot] = struct{}{}

		pDir, err := newPackageDirFromBlob(pkgRoot, fs)
		if err != nil {
			log.Printf("GC: failed getting package from blob %s: %s", pkgRoot, err)
			continue
		}

		for _, m := range pDir.Blobs() {
			allBlobs[m] = struct{}{}
		}
		pDir.Close()
	}

	log.Printf("GC: %d blobs referenced by %d packages", len(allBlobs), len(allPackageBlobs))

	for m := range allBlobs {
		delete(installedBlobs, m)
	}

	// remove all the blobs we no longer need
	log.Printf("GC: removing %d blobs from blobfs", len(installedBlobs))

	for m := range installedBlobs {
		e := os.Remove(path.Join("/blob", m))
		if e != nil {
			log.Printf("GC: error removing %s from blobfs: %s", m, e)
		}
	}
	return nil
}

// ValidateStaticIndex compares the contents of the static index against what
// blobs are available in blobfs. It returns the ids of the blobs that are
// present and those that are missing or any error encountered trying to do the
// validation.
func (fs *Filesystem) ValidateStaticIndex() (map[string]struct{}, map[string]struct{}, error) {
	installedBlobs, err := readBlobfs(fs.blobfs)
	if err != nil {
		return nil, nil, fmt.Errorf("pmd_validate: unable to list blobfs: %s", err)
	}

	present := make(map[string]struct{})
	missing := make(map[string]struct{})
	staticPkgs := fs.static.StaticPackageBlobs()
	if pd, ok := fs.root.dir("system").(*packageDir); ok {
		staticPkgs = append(staticPkgs, pd.contents["meta"])
	}

	for _, pkgRoot := range staticPkgs {
		if _, ok := installedBlobs[pkgRoot]; ok {
			present[pkgRoot] = struct{}{}
		} else {
			log.Printf("pmd_validate: %q root is missing", pkgRoot)
			missing[pkgRoot] = struct{}{}
		}

		pDir, err := newPackageDirFromBlob(pkgRoot, fs)
		if err != nil {
			log.Printf("pmd_validate: failed getting package from blob %s: %s", pkgRoot, err)
			pDir.Close()
			return nil, nil, err
		}

		for _, m := range pDir.Blobs() {
			if _, ok := installedBlobs[m]; ok {
				present[m] = struct{}{}
			} else {
				log.Printf("pmd_validate: %q is missing from %q", m, pkgRoot)
				missing[m] = struct{}{}
			}
		}
		pDir.Close()
	}
	return present, missing, nil
}

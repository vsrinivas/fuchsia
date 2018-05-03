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
	"os"
	"path/filepath"
	"sync"
	"syscall"
	"syscall/zx"
	"syscall/zx/fdio"
	"time"

	"app/context"
	"fidl/fuchsia/amber"
	"thinfs/fs"
	"thinfs/zircon/rpc"

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
	amberPxy  *amber.ControlInterface
}

// New initializes a new pkgfs filesystem server
func New(indexDir, blobDir string) (*Filesystem, error) {
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
			"packages": &packagesRoot{
				unsupportedDirectory: unsupportedDirectory("/packages"),
				fs:                   f,
			},
			"system":   unsupportedDirectory("/system"),
			"metadata": unsupportedDirectory("/metadata"),
		},
	}

	req, pxy, err := amber.NewControlInterfaceRequest()
	if err != nil {
		panic(err.Error())
	}
	context.CreateFromStartupInfo().ConnectToEnvService(req)
	f.amberPxy = pxy

	f.index.Notifier = pxy

	return f, nil
}

// staticIndexPath is the path inside the system package directory that contains the static packages for that system version.
const staticIndexPath = "data/static_packages"

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

	indexFile, err := f.blobfs.Open(blob)
	if err != nil {
		return fmt.Errorf("pkgfs: could not load static index %q from package %q: %s", staticIndexPath, merkleroot, err)
	}
	defer indexFile.Close()

	return f.static.LoadFrom(indexFile)
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

	handles := []zx.Handle{zx.Handle(mountChan)}
	if _, _, err := syscall.FDIOForFD(f.mountInfo.parentFd).Ioctl(fdio.IoctlVFSMountFS, 0, nil, handles); err != nil {
		mountChan.Close()
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
			syscall.FDIOForFD(f.mountInfo.parentFd).Ioctl(fdio.IoctlVFSUnmountNode, 0, nil, nil)
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
	case zx.Error:
		switch e.Status {
		case zx.ErrNotFound:
			return fs.ErrNotFound

		default:
			debugLog("pkgfs: unmapped os err to fs err: %T %v", err, err)
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

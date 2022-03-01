// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//go:build !build_with_native_toolchain
// +build !build_with_native_toolchain

package pkgfs

import (
	"encoding/binary"
	"fmt"
	"path/filepath"
	"sort"
	"strings"
	"time"

	"fidl/fuchsia/io"

	"go.fuchsia.dev/fuchsia/src/lib/thinfs/fs"
	"go.fuchsia.dev/fuchsia/src/sys/pkg/lib/far/go"

	"syscall"
	"syscall/zx"
	"syscall/zx/fdio"
	zxio "syscall/zx/io"
)

func newMetaFar(blob string, fs *Filesystem) (*metaFar, error) {
	mf := &metaFar{
		blob: blob,
		fs:   fs,
	}
	fr, err := mf.open()
	if err != nil {
		return nil, err
	}

	contents := fr.List()
	fr.Close()

	mf.contents = contents
	return mf, nil
}

// metaFar is a shared reference to a meta.far or one or more of it's contents.
type metaFar struct {
	blob     string
	contents []string

	fs *Filesystem
}

func (mf *metaFar) open() (*far.Reader, error) {
	f, err := mf.fs.blobfs.Open(mf.blob)
	if err != nil {
		return nil, err
	}

	fr, err := far.NewReader(f)
	if err != nil {
		f.Close()
	}
	return fr, err
}

func (mf *metaFar) list() []string {
	return mf.contents
}

// metaFile is the package dir "meta" opened as a file, which on read returns
// the merkleroot.
type metaFile struct {
	unsupportedFile

	*metaFar

	off   int64
	flags fs.OpenFlags
}

func newMetaFile(mf *metaFar, flags fs.OpenFlags) *metaFile {
	return &metaFile{
		unsupportedFile("package/meta:" + mf.blob),
		mf,
		0,
		flags,
	}
}

func (f *metaFile) Close() error {
	return nil
}

func (f *metaFile) GetOpenFlags() fs.OpenFlags {
	return f.flags
}

func (f *metaFile) Stat() (int64, time.Time, time.Time, error) {
	return int64(len(f.blob)), time.Time{}, time.Time{}, nil
}

func (f *metaFile) Read(p []byte, off int64, whence int) (int, error) {
	if whence != fs.WhenceFromCurrent {
		return 0, fs.ErrNotSupported
	}
	if f.off+off >= int64(len(f.blob)) {
		return 0, fs.ErrEOF
	}

	n := copy(p, f.blob[f.off+off:])
	f.off += off + int64(n)
	return n, nil
}

var _ fs.Directory = (*metaFarDir)(nil)

type metaFarDir struct {
	unsupportedDirectory

	*metaFar

	path string
}

func newMetaFarDir(mf *metaFar) *metaFarDir {
	return &metaFarDir{
		unsupportedDirectory("package/meta:" + mf.blob),
		mf,
		"meta",
	}
}

func newMetaFarDirAt(mf *metaFar, path string) *metaFarDir {
	mfd := newMetaFarDir(mf)
	mfd.path = path
	return mfd
}

func (d *metaFarDir) Close() error {

	return nil
}

func (d *metaFarDir) Dup() (fs.Directory, error) {
	return d, nil
}

func (d *metaFarDir) Open(name string, flags fs.OpenFlags) (fs.File, fs.Directory, *fs.Remote, error) {
	name = clean(name)

	// Nothing in the meta directory is ever executable.
	if flags.Execute() {
		return nil, nil, nil, fs.ErrNotSupported
	}

	name = filepath.Join(d.path, name)

	if name == "" {
		if flags.File() || (!flags.Directory() && !flags.Path()) {
			return newMetaFile(d.metaFar, flags), nil, nil, nil
		}
		return nil, d, nil, nil
	}

	if flags.Create() || flags.Truncate() || flags.Write() || flags.Append() {
		return nil, nil, nil, fs.ErrNotSupported
	}

	contents := d.metaFar.list()

	if n := sort.SearchStrings(contents, name); n != len(contents) && contents[n] == name {
		mff, err := newMetaFarFile(d.metaFar, name, flags)
		return mff, nil, nil, err
	}

	dname := name + "/"
	for _, lname := range contents {
		if strings.HasPrefix(lname, dname) {
			return nil, newMetaFarDirAt(d.metaFar, name), nil, nil
		}
	}

	return nil, nil, nil, fs.ErrNotFound
}

func (d *metaFarDir) Read() ([]fs.Dirent, error) {
	contents := d.metaFar.list()

	// TODO(raggi): improve efficiency
	dirs := map[string]struct{}{}
	dents := []fs.Dirent{}
	dents = append(dents, dirDirEnt("."))

	for _, name := range contents {
		if !strings.HasPrefix(name, d.path+"/") {
			continue
		}
		name = strings.TrimPrefix(name, d.path+"/")

		parts := strings.SplitN(name, "/", 2)
		if len(parts) == 2 {
			if _, ok := dirs[parts[0]]; !ok {
				dirs[parts[0]] = struct{}{}
				dents = append(dents, dirDirEnt(parts[0]))
			}

		} else {
			dents = append(dents, fileDirEnt(parts[0]))
		}
	}
	return dents, nil
}

func (d *metaFarDir) Stat() (int64, time.Time, time.Time, error) {
	// TODO(raggi): forward stat values from the index
	contents := d.metaFar.list()
	return int64(len(contents)), d.fs.mountTime, d.fs.mountTime, nil
}

var _ fs.File = (*metaFarFile)(nil)
var _ fs.FileWithBackingMemory = (*metaFarFile)(nil)

type metaFarFile struct {
	unsupportedFile

	*metaFar
	fr *far.Reader
	er *far.EntryReader

	off   int64
	path  string
	flags fs.OpenFlags
	// VMO representing this file's view of the archive.
	vmo *zx.VMO

	// VMO representing the blob backing this entire archive.
	// TODO(fxbug.dev/52938): It would be more efficient to cache this VMO for all files
	// within the same meta far to avoid calling GetBuffer() on the same blob
	// for multiple files.
	backingBlobVMO *zx.VMO
}

func newMetaFarFile(mf *metaFar, path string, flags fs.OpenFlags) (*metaFarFile, error) {
	fr, err := mf.open()
	if err != nil {
		return nil, goErrToFSErr(err)
	}
	er, err := fr.Open(path)
	if err != nil {
		fr.Close()
		return nil, goErrToFSErr(err)
	}

	return &metaFarFile{
		unsupportedFile("package/meta:" + mf.blob + "/" + path),
		mf,
		fr,
		er,
		0,
		path,
		flags,
		nil,
		nil,
	}, nil
}

func (f *metaFarFile) Close() error {
	if f.vmo != nil {
		f.vmo.Close()
	}
	if f.backingBlobVMO != nil {
		f.backingBlobVMO.Close()
	}
	f.fr.Close()
	return nil
}

func (f *metaFarFile) GetOpenFlags() fs.OpenFlags {
	return f.flags
}

func (f *metaFarFile) Dup() (fs.File, error) {
	fr, err := f.metaFar.open()
	if err != nil {
		return nil, goErrToFSErr(err)
	}
	er, err := fr.Open(f.path)
	if err != nil {
		fr.Close()
		return nil, goErrToFSErr(err)
	}

	return &metaFarFile{
		f.unsupportedFile,
		f.metaFar,
		fr,
		er,
		0,
		f.path,
		f.flags,
		nil,
		nil,
	}, nil
}

func (f *metaFarFile) Read(p []byte, off int64, whence int) (int, error) {
	// TODO(raggi): this could allocate less/be far more efficient

	switch whence {
	case fs.WhenceFromCurrent:
		f.off += off
		n, err := f.er.ReadAt(p, f.off)
		f.off += int64(n)
		return n, goErrToFSErr(err)
	case fs.WhenceFromStart:
		return f.er.ReadAt(p, off)
	}
	return 0, fs.ErrNotSupported
}

func (f *metaFarFile) Seek(offset int64, whence int) (int64, error) {
	var err error
	switch whence {
	case fs.WhenceFromCurrent:
		f.off = f.off + offset
	case fs.WhenceFromStart:
		f.off = offset
	case fs.WhenceFromEnd:
		f.off = int64(f.er.Length) + offset
	default:
		return 0, fs.ErrInvalidArgs
	}
	if err != nil {
		return f.off, goErrToFSErr(err)
	}
	return f.off, nil
}

func (f *metaFarFile) Stat() (int64, time.Time, time.Time, error) {
	return int64(f.er.Length), time.Time{}, time.Time{}, nil
}

func (mf *metaFarFile) getBackingBlobVMO() (*zx.VMO, error) {
	if mf.backingBlobVMO != nil {
		return mf.backingBlobVMO, nil
	}
	f, err := mf.fs.blobfs.Open(mf.blob)
	if err != nil {
		return nil, err
	}
	defer f.Close()

	fdioFile := syscall.FDIOForFD(int(f.Fd())).(*fdio.File)
	result, err := fdioFile.GetBackingMemory(zxio.VmoFlagsRead)
	if err != nil {
		return nil, err
	}
	switch w := result.Which(); w {
	case zxio.File2GetBackingMemoryResultErr:
		return nil, &zx.Error{
			Status: zx.Status(result.Err),
			Text:   "File.GetBackingMemory",
		}
	case zxio.File2GetBackingMemoryResultResponse:
		mf.backingBlobVMO = &result.Response.Vmo
		return mf.backingBlobVMO, nil
	default:
		panic(fmt.Sprintf("unhandle variant %d", w))
	}
}

func (f *metaFarFile) GetBackingMemory(flags io.VmoFlags) (*zx.VMO, uint64, error) {
	size := f.er.Length
	if f.vmo == nil {
		parentVmo, err := f.getBackingBlobVMO()
		if err != nil {
			return nil, 0, fs.ErrIO
		}
		// All entries in a FAR are at 4096 byte offsets from the start of the
		// file and are zero padded up to the next 4096 byte boundary:
		// https://fuchsia.dev/fuchsia-src/concepts/source_code/archive_format#content_chunk
		offset := f.er.Offset
		options := zx.VMOChildOption(zx.VMOChildOptionSnapshotAtLeastOnWrite | zx.VMOChildOptionNoWrite)

		vmo, err := parentVmo.CreateChild(options, offset, size)
		if err != nil {
			return nil, 0, fs.ErrIO
		}
		f.vmo = &vmo
	}

	rights := zx.RightsBasic | zx.RightMap | zx.RightsProperty

	if flags&io.VmoFlagsRead != 0 {
		rights |= zx.RightRead
	}
	if flags&io.VmoFlagsWrite != 0 {
		// Contents of a meta directory are never writable.
		return nil, 0, fs.ErrReadOnly
	}
	if flags&io.VmoFlagsExecute != 0 {
		// Contents of a meta directory are never executable.
		return nil, 0, fs.ErrPermission
	}

	if flags&io.VmoFlagsSharedBuffer != 0 {
		return nil, 0, fs.ErrNotSupported
	}

	if flags&io.VmoFlagsPrivateClone != 0 {
		// Create a separate VMO for the caller if they specified that they want a private copy.

		options := zx.VMOChildOption(zx.VMOChildOptionSnapshotAtLeastOnWrite)
		options |= zx.VMOChildOptionNoWrite
		offset := uint64(0)
		child, err := f.vmo.CreateChild(options, offset, size)
		if err != nil {
			return nil, 0, fs.ErrIO
		}
		return &child, size, nil
	}

	// Otherwise, just duplicate our VMO.
	h, err := f.vmo.Handle().Duplicate(rights)
	if err != nil {
		return nil, 0, fs.ErrPermission
	}
	var sizeBytes [8]byte
	binary.LittleEndian.PutUint64(sizeBytes[:], size)
	if err := h.SetProperty(zx.PropVmoContentSize, sizeBytes[:]); err != nil {
		return nil, 0, err
	}
	vmo := zx.VMO(h)
	return &vmo, size, nil
}

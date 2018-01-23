package pkgfs

import (
	"fmt"
	"os"
	"path/filepath"
	"strings"
	"time"

	"thinfs/fs"

	"fuchsia.googlesource.com/far"
)

func GetMetaFar(name, version, blob string, fs *Filesystem) (*metaFar, error) {
	f, err := os.Open(filepath.Join(fs.blobstore.Root, blob))
	if err != nil {
		return nil, err
	}
	fr, err := far.NewReader(f)
	if err != nil {
		return nil, err
	}

	mf := &metaFar{
		name:    name,
		version: version,
		blob:    blob,
		fs:      fs,

		fr: fr,
	}

	return mf, nil
}

// metaFar is a shared reference to an open meta.far or one or more of it's contents.
type metaFar struct {
	name, version string // of the package
	blob          string

	fs *Filesystem

	fr *far.Reader
}

type metaFarDir struct {
	unsupportedDirectory

	*metaFar

	path string
}

func newMetaFarDir(name, version, blob string, fs *Filesystem) (*metaFarDir, error) {
	mf, err := GetMetaFar(name, version, blob, fs)
	if err != nil {
		debugLog(fmt.Sprintf("pkgfs:newMetaFarDir: %s", err))
		return nil, err
	}
	return &metaFarDir{
		unsupportedDirectory(fmt.Sprintf("pkgfs:meta.far:%s/%s@%s", name, version, blob)),
		mf,
		"meta",
	}, nil
}

func newMetaFarDirAt(name, version, blob string, fs *Filesystem, path string) (*metaFarDir, error) {
	mf, err := newMetaFarDir(name, version, blob, fs)
	if err != nil {
		debugLog(fmt.Sprintf("pkgfs:newMetaFarDirAt: %s", err))
		return nil, err
	}
	mf.path = filepath.Join("meta", path)
	return mf, nil
}

func (d *metaFarDir) Close() error {
	debugLog("pkgfs:metaFarDir:close %q/%q@%s", d.name, d.version, d.blob)
	return nil
}

func (d *metaFarDir) Dup() (fs.Directory, error) {
	return d, nil
}

func (d *metaFarDir) Reopen(flags fs.OpenFlags) (fs.Directory, error) {
	return d, nil
}

func (d *metaFarDir) Open(name string, flags fs.OpenFlags) (fs.File, fs.Directory, *fs.Remote, error) {
	name = clean(name)
	debugLog("pkgfs:metaFarDir:open %q", name)

	if name == "" {
		return nil, d, nil, nil
	}

	name = filepath.Join(d.path, name)

	if flags.Create() || flags.Truncate() || flags.Write() || flags.Append() {
		debugLog("pkgfs:metaFarDir:open %q unsupported flags", name)
		return nil, nil, nil, fs.ErrNotSupported
	}

	for _, lname := range d.fr.List() {
		if name == lname {
			mff, err := newMetaFarFile(d.name, d.version, d.blob, d.fs, name)
			if err != nil {
				return nil, nil, nil, err
			}
			return mff, nil, nil, nil
		}
	}

	dname := name + "/"
	for _, lname := range d.fr.List() {
		if strings.HasPrefix(lname, dname) {
			mfd, err := newMetaFarDirAt(d.name, d.version, d.blob, d.fs, name)
			if err != nil {
				return nil, nil, nil, err
			}
			return nil, mfd, nil, nil
		}
	}

	debugLog("pkgfs:metaFarDir:open %q not found", name)
	return nil, nil, nil, fs.ErrNotFound
}

func (d *metaFarDir) Read() ([]fs.Dirent, error) {
	debugLog("pkgfs:metaFarDir:read %q %q", d.blob, d.path)

	// TODO(raggi): improve efficiency
	dirs := map[string]struct{}{}
	dents := []fs.Dirent{}
	dents = append(dents, dirDirEnt("."))
	for _, name := range d.fr.List() {
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
	debugLog("pkgfs:metaFarDir:stat %q/%q@%s", d.name, d.version, d.blob)
	// TODO(raggi): forward stat values from the index
	return int64(len(d.fr.List())), d.fs.mountTime, d.fs.mountTime, nil
}

type metaFarFile struct {
	unsupportedFile

	*metaFar

	off  int64
	path string
}

func newMetaFarFile(name, version, blob string, fs *Filesystem, path string) (*metaFarFile, error) {
	debugLog("pkgfs:metaFarFile:new %q/%s", blob, path)

	mf, err := GetMetaFar(name, version, blob, fs)
	if err != nil {
		return nil, err
	}

	mff := &metaFarFile{
		unsupportedFile(fmt.Sprintf("pkgfs:metaFarFile:%s/%s/%s", name, version, path)),
		mf,
		0,
		path,
	}
	return mff, nil
}

func (f *metaFarFile) Close() error {
	debugLog("pkgfs:metaFarFile:close %q/%s", f.blob, f.path)
	return nil
}

func (f *metaFarFile) Dup() (fs.File, error) {
	debugLog("pkgfs:metaFarFile:dup %q/%s", f.blob, f.path)
	return &metaFarFile{
		f.unsupportedFile,
		f.metaFar,
		0,
		f.path,
	}, nil
}

func (f *metaFarFile) Reopen(flags fs.OpenFlags) (fs.File, error) {
	debugLog("pkgfs:metaFarFile:reopen %q/%s", f.blob, f.path)
	f.off = 0
	return f, nil
}

func (f *metaFarFile) Read(p []byte, off int64, whence int) (int, error) {
	debugLog("pkgfs:metaFarFile:read %q/%s - %d %d", f.blob, f.path, off, whence)
	// TODO(raggi): this could allocate less/be far more efficient

	ra, err := f.fr.Open(f.path)
	if err != nil {
		debugLog("pkgfs:metaFarFile:read %q/%s - %s", f.blob, f.path, err)
		return 0, goErrToFSErr(err)
	}

	switch whence {
	case fs.WhenceFromCurrent:
		f.off += off
		n, err := ra.ReadAt(p, f.off)
		f.off += int64(n)
		return n, goErrToFSErr(err)
	case fs.WhenceFromStart:
		return ra.ReadAt(p, off)
	}
	return 0, fs.ErrNotSupported
}

func (f *metaFarFile) Seek(offset int64, whence int) (int64, error) {
	debugLog("pkgfs:metaFarFile:seek %q/%s", f.blob, f.path)
	var err error
	var n int64
	switch whence {
	case fs.WhenceFromCurrent:
		f.off = f.off + offset
	case fs.WhenceFromStart:
		f.off = offset
	case fs.WhenceFromEnd:
		err = fs.ErrNotSupported
	default:
		return 0, fs.ErrInvalidArgs
	}
	if err != nil {
		return f.off, goErrToFSErr(err)
	}
	return n, nil
}

func (f *metaFarFile) Stat() (int64, time.Time, time.Time, error) {
	debugLog("pkgfs:metaFarFile:stat")
	return int64(f.fr.GetSize(f.path)), time.Time{}, time.Time{}, nil
}

// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Package pkgfs hosts a filesystem for interacting with packages that are
// stored on a host. It presents a tree of packages that are locally available
// and a tree that enables a user to add new packages and/or package content to
// the host.
package pkgfs

import (
	"bufio"
	"bytes"
	"encoding/json"
	"io"
	"io/ioutil"
	"log"
	"os"
	"path/filepath"
	"strings"
	"sync"
	"time"

	"thinfs/fs"

	"fuchsia.googlesource.com/pm/blobstore"
	"fuchsia.googlesource.com/pm/far"
	"fuchsia.googlesource.com/pm/index"
	"fuchsia.googlesource.com/pm/pkg"
)

// Filesystem is the top level container for a pkgfs server
type Filesystem struct {
	root      fs.Directory
	index     *index.Index
	blobstore *blobstore.Manager
	mountInfo mountInfo
	mountTime time.Time
}

// New initializes a new pkgfs filesystem server
func New(indexDir, blobstoreDir string) (*Filesystem, error) {
	index, err := index.New(indexDir)
	if err != nil {
		return nil, err
	}

	bm, err := blobstore.New(blobstoreDir, "")
	f := &Filesystem{
		index:     index,
		blobstore: bm,
	}

	f.root = &rootDirectory{
		unsupportedDirectory: unsupportedDirectory("/"),
		fs:                   f,

		dirs: map[string]fs.Directory{
			"incoming": &inDirectory{
				unsupportedDirectory: unsupportedDirectory("/incoming"),
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
			"metadata": unsupportedDirectory("/metadata"),
		},
	}

	return f, nil
}

func (f *Filesystem) Blockcount() int64 {
	// TODO(raggi): sum up all packages?
	// TODO(raggi): delegate to blobstore?
	debugLog("fs blockcount")
	return 0
}

func (f *Filesystem) Blocksize() int64 {
	// TODO(raggi): sum up all packages?
	// TODO(raggi): delegate to blobstore?
	debugLog("fs blocksize")
	return 0
}

func (f *Filesystem) Size() int64 {
	debugLog("fs size")
	// TODO(raggi): delegate to blobstore?
	return 0
}

func (f *Filesystem) Close() error {
	debugLog("fs close")
	return fs.ErrNotSupported
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

var _ fs.FileSystem = (*Filesystem)(nil)

type rootDirectory struct {
	unsupportedDirectory
	fs   *Filesystem
	dirs map[string]fs.Directory
}

func (d *rootDirectory) Open(name string, flags fs.OpenFlags) (fs.File, fs.Directory, error) {
	name = clean(name)
	if name == "" {
		return nil, d, nil
	}

	parts := strings.SplitN(name, "/", 2)

	subdir, ok := d.dirs[parts[0]]
	if !ok {
		return nil, nil, fs.ErrNotFound
	}

	if len(parts) == 1 {
		return nil, subdir, nil
	}

	return subdir.Open(parts[1], flags)
}

func (d *rootDirectory) Read() ([]fs.Dirent, error) {
	debugLog("pkgfs:root:read")

	dirs := make([]fs.Dirent, 0, len(d.dirs))
	for n := range d.dirs {
		dirs = append(dirs, dirDirEnt(n))
	}
	return dirs, nil
}

func (d *rootDirectory) Close() error {
	debugLog("pkgfs:root:close")
	return nil
}

func (d *rootDirectory) Stat() (int64, time.Time, time.Time, error) {
	debugLog("pkgfs:root:stat")
	return 0, d.fs.mountTime, d.fs.mountTime, nil
}

// inDirectory is located at /in. It accepts newly created files, and writes them to blobstore.
type inDirectory struct {
	unsupportedDirectory

	fs *Filesystem
}

func (d *inDirectory) Stat() (int64, time.Time, time.Time, error) {
	debugLog("pkgfs:in:stat")
	return 0, d.fs.mountTime, d.fs.mountTime, nil
}

func (d *inDirectory) Open(name string, flags fs.OpenFlags) (fs.File, fs.Directory, error) {
	name = clean(name)
	debugLog("pkgfs:in:open %q", name)
	if name == "" {
		return nil, d, nil
	}

	if !(flags.Create() && flags.File()) {
		return nil, nil, fs.ErrNotFound
	}
	// TODO(raggi): validate/reject other flags

	// TODO(raggi): create separate incoming directories for blobs and packages
	return &inFile{unsupportedFile: unsupportedFile("/pkgfs/incoming/" + name), fs: d.fs, oname: name, name: ""}, nil, nil
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
		f.w, err = f.fs.blobstore.Create(f.name, f.sz)
		if err != nil {
			log.Printf("pkgfs: incoming file %q blobstore creation failed %q: %s", f.oname, f.name, err)
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
		f.w, err = f.fs.blobstore.Create(f.name, f.sz)
		if err != nil {
			log.Printf("pkgfs: incoming file %q blobstore creation failed %q: %s", f.oname, f.name, err)
			return goErrToFSErr(err)
		}
	}

	err := f.w.Close()

	root := f.name
	if rooter, ok := f.w.(blobstore.Rooter); ok {
		root, err = rooter.Root()
		if err != nil {
			log.Printf("pkgfs: root digest computation failed after successful blobstore write: %s", err)
			return goErrToFSErr(err)
		}
	}

	if err == nil || f.fs.blobstore.HasBlob(root) {
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
		log.Printf("pkgfs: failed incoming file write to blobstore: %s", err)
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

// importPackage reads a package far from blobstore, given a content key, and imports it into the package index
func importPackage(fs *Filesystem, root string) {
	log.Printf("pkgfs: importing package from %q", root)

	f, err := fs.blobstore.Open(root)
	if err != nil {
		log.Printf("error importing package: %s", err)
		return
	}
	defer f.Close()

	// TODO(raggi): this is a bit messy, the system could instead force people to
	// write to specific paths in the incoming directory
	if !far.IsFAR(f) {
		log.Printf("%q is not a package, ignoring import", root)
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

	pf, err := r.ReadFile("meta/package.json")
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
		log.Printf("error parsing package contents file: %s", err)
		return
	}

	pkgWaitingDir := fs.index.WaitingPackageVersionPath(p.Name, p.Version)

	files := bytes.Split(contents, []byte{'\n'})
	var needsCount int
	for i := range files {
		parts := bytes.SplitN(files[i], []byte{'='}, 2)
		if len(parts) != 2 {
			// TODO(raggi): log illegal contents format?
			continue
		}
		root := string(parts[1])

		if fs.blobstore.HasBlob(root) {
			log.Printf("pkgfs: blob already present %q", root)
			continue
		}

		log.Printf("pkgfs: blob needed %q", root)

		needsCount++
		err = ioutil.WriteFile(fs.index.NeedsBlob(root), []byte{}, os.ModePerm)
		if err != nil {
			// XXX(raggi): there are potential deadlock conditions here, we should fail the package write (???)
			log.Printf("pkgfs: import error, can't create needs index: %s", err)
		}

		if needsCount == 1 {
			os.MkdirAll(pkgWaitingDir, os.ModePerm)
		}

		err = ioutil.WriteFile(filepath.Join(pkgWaitingDir, root), []byte{}, os.ModePerm)
		if err != nil {
			log.Printf("pkgfs: import error, can't create needs index: %s", err)
		}
	}

	// TODO(raggi): validate the package names, ensure they do not contain '/', '=', and are neither '.' or '..'

	if needsCount == 0 {
		pkgIndexDir := fs.index.PackagePath(p.Name)
		os.MkdirAll(pkgIndexDir, os.ModePerm)

		if err := ioutil.WriteFile(filepath.Join(pkgIndexDir, p.Version), contents, os.ModePerm); err != nil {
			// XXX(raggi): is this a really bad state?
			log.Printf("pkgfs: error writing package installed index for %s/%s: %s", p.Name, p.Version, err)
		}
	}

	// XXX(raggi): there's a potential race here where needs could be fulfilled
	// before this is written, so this should get re-orgnized to execute before the
	// needs files are written, and then the move should be done as if checkNeeds
	// completed.
	pkgInstalling := fs.index.InstallingPackageVersionPath(p.Name, p.Version)
	os.MkdirAll(filepath.Dir(pkgInstalling), os.ModePerm)

	if err := ioutil.WriteFile(pkgInstalling, contents, os.ModePerm); err != nil {
		log.Printf("error writing package installing index for %s/%s: %s", p.Name, p.Version, err)
	}
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

			from := filepath.Join(fs.index.InstallingDir(), pkgNameVersion)
			to := filepath.Join(fs.index.PackagesDir(), pkgNameVersion)
			os.MkdirAll(filepath.Dir(to), os.ModePerm)
			debugLog("package %s ready, moving %s to %s", pkgNameVersion, from, to)
			if err := os.Rename(from, to); err != nil {
				// TODO(raggi): this kind of state will need to be cleaned up by a general garbage collector at a later time.
				log.Printf("pkgfs: error moving package from installing to packages: %s", err)
			}
		}
	}
}

type packagesRoot struct {
	unsupportedDirectory

	fs *Filesystem
}

func (pr *packagesRoot) Close() error { return nil }

func (pr *packagesRoot) Open(name string, flags fs.OpenFlags) (fs.File, fs.Directory, error) {
	name = clean(name)
	debugLog("pkgfs:packagesroot:open %q", name)
	if name == "" {
		return nil, pr, nil
	}

	parts := strings.Split(name, "/")

	pld, err := newPackageListDir(parts[0], pr.fs)
	if err != nil {
		log.Printf("pkgfs:packagesroot:open error reading package list dir for %q: %s", name, err)
		return nil, nil, err
	}
	if len(parts) > 1 {
		debugLog("pkgfs:packagesroot:open forwarding %v to %q", parts[1:], name)
		return pld.Open(filepath.Join(parts[1:]...), flags)
	}
	return nil, pld, nil
}

func (pr *packagesRoot) Read() ([]fs.Dirent, error) {
	debugLog("pkgfs:packagesroot:read")

	names, err := filepath.Glob(pr.fs.index.PackagePath("*"))
	if err != nil {
		return nil, goErrToFSErr(err)
	}
	dirents := make([]fs.Dirent, len(names))
	for i := range names {
		dirents[i] = fileDirEnt(filepath.Base(names[i]))
	}
	return dirents, nil
}

func (pr *packagesRoot) Stat() (int64, time.Time, time.Time, error) {
	debugLog("pkgfs:packagesRoot:stat")
	// TODO(raggi): stat the index directory and pass on info
	return 0, pr.fs.mountTime, pr.fs.mountTime, nil
}

// packageListDir is a directory in the pkgfs packages directory for an
// individual package that lists all versions of packages
type packageListDir struct {
	unsupportedDirectory
	fs          *Filesystem
	packageName string
}

func newPackageListDir(name string, f *Filesystem) (*packageListDir, error) {
	debugLog("pkgfs:newPackageListDir: %q", name)
	_, err := os.Stat(f.index.PackagePath(name))
	if os.IsNotExist(err) {
		debugLog("pkgfs:newPackageListDir: %q not found", name)
		return nil, fs.ErrNotFound
	}
	if err != nil {
		log.Printf("pkgfs: error opening package: %q: %s", name, err)
		return nil, err
	}
	pld := packageListDir{
		unsupportedDirectory: unsupportedDirectory(filepath.Join("/packages", name)),
		fs:                   f,
		packageName:          name,
	}
	return &pld, nil
}

func (pld *packageListDir) Close() error {
	debugLog("pkgfs:packageListDir:close %q", pld.packageName)
	return nil
}

func (pld *packageListDir) Open(name string, flags fs.OpenFlags) (fs.File, fs.Directory, error) {
	name = clean(name)
	debugLog("pkgfs:packageListDir:open %q %s", pld.packageName, name)

	parts := strings.Split(name, "/")

	d, err := newPackageDir(pld.packageName, parts[0], pld.fs)
	if err != nil {
		return nil, nil, err
	}

	if len(parts) > 1 {
		return d.Open(filepath.Join(parts[1:]...), flags)
	}
	return nil, d, nil
}

func (pld *packageListDir) Read() ([]fs.Dirent, error) {
	debugLog("pkgfs:packageListdir:read %q", pld.packageName)

	names, err := filepath.Glob(pld.fs.index.PackageVersionPath(pld.packageName, "*"))
	if err != nil {
		return nil, goErrToFSErr(err)
	}
	dirents := make([]fs.Dirent, len(names))
	for i := range names {
		dirents[i] = fileDirEnt(filepath.Base(names[i]))
	}
	return dirents, nil
}

func (pld *packageListDir) Stat() (int64, time.Time, time.Time, error) {
	debugLog("pkgfs:packageListDir:stat %q", pld.packageName)
	// TODO(raggi): stat the index directory and pass on info
	return 0, pld.fs.mountTime, pld.fs.mountTime, nil
}

type packageDir struct {
	unsupportedDirectory
	fs            *Filesystem
	name, version string
	contents      map[string]string
}

func newPackageDir(name, version string, filesystem *Filesystem) (*packageDir, error) {
	packageIndex := filesystem.index.PackageVersionPath(name, version)

	pd := packageDir{
		unsupportedDirectory: unsupportedDirectory(filepath.Join("/packages", name, version)),
		fs:                   filesystem,
		name:                 name,
		version:              version,
		contents:             map[string]string{},
	}

	f, err := os.Open(packageIndex)
	if err != nil {
		log.Printf("pkgfs: failed to open package contents at %q: %s", packageIndex, err)
		return nil, goErrToFSErr(err)
	}
	b := bufio.NewReader(f)
	for {
		line, err := b.ReadString('\n')
		if err == io.EOF {
			break
		}
		if err != nil {
			f.Close()
			log.Printf("pkgfs: failed to read package contents from %q: %s", packageIndex, err)
			// TODO(raggi): better error?
			return nil, fs.ErrFailedPrecondition
		}
		parts := strings.SplitN(line[:len(line)-1], "=", 2)
		pd.contents[parts[0]] = parts[1]
	}
	f.Close()
	return &pd, nil
}

func (d *packageDir) Close() error {
	debugLog("pkgfs:packageDir:close %q/%q", d.name, d.version)
	return nil
}

func (d *packageDir) Open(name string, flags fs.OpenFlags) (fs.File, fs.Directory, error) {
	name = clean(name)
	debugLog("pkgfs:packagedir:open %q", name)
	if name == "" {
		return nil, d, nil
	}

	if flags.Create() || flags.Truncate() || flags.Write() || flags.Append() {
		return nil, nil, fs.ErrNotSupported
	}

	if root, ok := d.contents[name]; ok {
		f, err := d.fs.blobstore.Open(root)
		if err != nil {
			log.Printf("pkgfs: package file open failure: %q for %q/%q/%q", root, d.name, d.version, name)
			return nil, nil, goErrToFSErr(err)
		}
		return &packageFile{f, unsupportedFile("packagefile:" + root)}, nil, nil
	}
	log.Printf("pkgfs:packagedir:open %q not found in %v", name, d.contents)
	return nil, nil, fs.ErrNotFound
}

func (d *packageDir) Read() ([]fs.Dirent, error) {
	// TODO(raggi): improve efficiency
	dirs := map[string]struct{}{}
	dents := []fs.Dirent{}
	for name := range d.contents {
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

func (d *packageDir) Stat() (int64, time.Time, time.Time, error) {
	debugLog("pkgfs:packagedir:stat %q/%q", d.name, d.version)
	// TODO(raggi): forward stat values from the index
	return 0, d.fs.mountTime, d.fs.mountTime, nil
}

// TODO(raggi): turn this into a proper remoteio to the blobstore file instead.
type packageFile struct {
	*os.File
	unsupportedFile
}

func (pf *packageFile) Close() error {
	return goErrToFSErr(pf.File.Close())
}

func (pf *packageFile) Dup() (fs.File, error) {
	debugLog("pkgfs:packageFile:dup")
	return nil, fs.ErrNotSupported
}

func (pf *packageFile) Read(p []byte, off int64, whence int) (int, error) {
	// TODO(raggi): map os IO errors to fs errors
	switch whence {
	case fs.WhenceFromCurrent:
		if off != 0 {
			if _, err := pf.File.Seek(off, io.SeekCurrent); err != nil {
				return 0, goErrToFSErr(err)
			}
		}
		n, err := pf.File.Read(p)
		return n, goErrToFSErr(err)
	case fs.WhenceFromStart:
		return pf.File.ReadAt(p, off)
	}
	return 0, fs.ErrNotSupported
}

func (pf *packageFile) Reopen(flags fs.OpenFlags) (fs.File, error) {
	debugLog("pkgfs:packageFile:reopen")
	return nil, fs.ErrNotSupported
}

func (pf *packageFile) Seek(offset int64, whence int) (int64, error) {
	debugLog("pkgfs:packageFile:seek")
	var w int // os whence
	switch whence {
	case fs.WhenceFromCurrent:
		w = io.SeekCurrent
	case fs.WhenceFromStart:
		w = io.SeekStart
	case fs.WhenceFromEnd:
		w = io.SeekEnd
	default:
		return 0, fs.ErrInvalidArgs
	}
	n, err := pf.File.Seek(offset, w)
	return n, goErrToFSErr(err)
}

func (pf *packageFile) Stat() (int64, time.Time, time.Time, error) {
	debugLog("pkgfs:packageFile:stat")
	info, err := pf.File.Stat()
	if err != nil {
		// TODO(raggi): map errors
		return 0, time.Time{}, time.Time{}, err
	}
	return info.Size(), time.Time{}, info.ModTime(), nil
}

func (pf *packageFile) Sync() error {
	debugLog("pkgfs:packageFile:sync")
	return goErrToFSErr(pf.File.Sync())
}

func (pf *packageFile) Tell() (int64, error) {
	debugLog("pkgfs:packageFile:tell")
	return 0, fs.ErrNotSupported
}

func (pf *packageFile) Touch(lastAccess, lastModified time.Time) error {
	debugLog("pkgfs:packageFile:touch")
	return fs.ErrNotSupported
}

func (pf *packageFile) Truncate(size uint64) error {
	debugLog("pkgfs:packageFile:truncate")
	return fs.ErrNotSupported
}

func (pf *packageFile) Write(p []byte, off int64, whence int) (int, error) {
	debugLog("pkgfs:packageFile:write")
	return 0, fs.ErrNotSupported
}

type dirDirEnt string

func (d dirDirEnt) GetType() fs.FileType {
	return fs.FileTypeDirectory
}

func (d dirDirEnt) GetName() string {
	return string(d)
}

type fileDirEnt string

func (d fileDirEnt) GetType() fs.FileType {
	return fs.FileTypeRegularFile
}

func (d fileDirEnt) GetName() string {
	return string(d)
}

type needsRoot struct {
	unsupportedDirectory

	fs *Filesystem
}

func (d *needsRoot) Close() error {
	return nil
}

func (d *needsRoot) Open(name string, flags fs.OpenFlags) (fs.File, fs.Directory, error) {
	name = clean(name)
	if name == "" {
		return nil, d, nil
	}

	parts := strings.SplitN(name, "/", 2)

	switch parts[0] {
	case "blobs":
		nbd := &needsBlobsDir{unsupportedDirectory: unsupportedDirectory("/needs/blobs"), fs: d.fs}
		if len(parts) > 1 {
			return nbd.Open(parts[1], flags)
		}
		return nil, nbd, nil
	default:
		if len(parts) != 1 {
			return nil, nil, fs.ErrNotSupported
		}

		idxPath := d.fs.index.NeedsFile(parts[0])

		var f *os.File
		var err error
		if flags.Create() {
			f, err = os.Create(idxPath)
		} else {
			f, err = os.Open(idxPath)
		}
		if err != nil {
			return nil, nil, goErrToFSErr(err)
		}
		if err := f.Close(); err != nil {
			return nil, nil, goErrToFSErr(err)
		}
		return &needsFile{unsupportedFile: unsupportedFile(filepath.Join("/needs", name)), fs: d.fs}, nil, nil
	}
}

func (d *needsRoot) Read() ([]fs.Dirent, error) {
	infos, err := ioutil.ReadDir(d.fs.index.NeedsDir())
	if err != nil {
		return nil, goErrToFSErr(err)
	}

	var dents = make([]fs.Dirent, len(infos))

	for i, info := range infos {
		if info.IsDir() {
			dents[i] = dirDirEnt(filepath.Base(info.Name()))
		} else {
			dents[i] = fileDirEnt(filepath.Base(info.Name()))
		}
	}

	return dents, nil
}

func (d *needsRoot) Stat() (int64, time.Time, time.Time, error) {
	// TODO(raggi): provide more useful values
	return 0, d.fs.mountTime, d.fs.mountTime, nil
}

type needsFile struct {
	unsupportedFile

	fs *Filesystem
}

func (f *needsFile) Close() error {
	return nil
}

func (f *needsFile) Stat() (int64, time.Time, time.Time, error) {
	return 0, time.Time{}, time.Time{}, nil
}

type needsBlobsDir struct {
	unsupportedDirectory

	fs *Filesystem
}

func (d *needsBlobsDir) Close() error {
	return nil
}

func (d *needsBlobsDir) Open(name string, flags fs.OpenFlags) (fs.File, fs.Directory, error) {
	name = clean(name)
	if name == "" {
		return nil, d, nil
	}

	if strings.Contains(name, "/") {
		return nil, nil, fs.ErrNotFound
	}

	if _, err := os.Stat(d.fs.index.NeedsBlob(name)); err != nil {
		return nil, nil, goErrToFSErr(err)
	}

	debugLog("pkgfs:needsblob:%q open", name)
	return &inFile{unsupportedFile: unsupportedFile("/needs/blobs/" + name), fs: d.fs, oname: name, name: name}, nil, nil
}

func (d *needsBlobsDir) Read() ([]fs.Dirent, error) {
	names, err := filepath.Glob(d.fs.index.NeedsBlob("*"))
	if err != nil {
		return nil, goErrToFSErr(err)
	}
	dirents := make([]fs.Dirent, len(names))
	for i := range names {
		dirents[i] = fileDirEnt(filepath.Base(names[i]))
	}
	return dirents, nil
}

func (d *needsBlobsDir) Stat() (int64, time.Time, time.Time, error) {
	// TODO(raggi): provide more useful values
	return 0, d.fs.mountTime, d.fs.mountTime, nil
}

// clean canonicalizes a path and returns a path that is relative to an assumed root.
// as a result of this cleaning operation, an open of '/' or '.' or '' all return ''.
// TODO(raggi): speed this up/reduce allocation overhead.
func clean(path string) string {
	return filepath.Clean("/" + path)[1:]
}

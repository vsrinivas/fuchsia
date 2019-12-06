// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

package cache

import (
	"context"
	"encoding/hex"
	"encoding/json"
	"io"
	"io/ioutil"
	"math/rand"
	"os"
	"path/filepath"
	"sort"
	"sync/atomic"
	"time"
)

// the time in milliseconds to wait before checking the size of the directory
// that contains cache files.
const waitBeforeReadDir = 32

// FileKey is the interface of keys that can be used to index cached files.
type FileKey interface {
	// Hash should return a valid unique file name to be used as the key.
	Hash() string
}

// LinkedFile removes the file it names on closing. This is used to link files
// into a temporary directory while they're being used so that collection of
// old files doesn't delete a file that's in use. This effectivelly uses the
// inode count as a reference counter for the file.
type LinkedFile string

// String returns the name of the file.
func (l LinkedFile) String() string {
	return string(l)
}

// Close deletes the file.
func (l LinkedFile) Close() error {
	return os.Remove(l.String())
}

type config struct {
	// Size is the maximum number of entries before an item is evicted.
	// Zero means no limt on the number of entries.
	Size uint64
}

// FileCache represents a cache of files stored somewhere on the disk. It is
// possible to use the cache concurrently from any number of processes or
// threads.
type FileCache struct {
	dir      string
	cfg      config
	curSize  uint64
	cullLock chan struct{}
}

func getTmpDir(path string) string {
	return filepath.Join(path, "tmp")
}

func getTmpFile(path string) string {
	out := make([]byte, 8)
	rand.Read(out)
	return filepath.Join(path, "tmp", hex.EncodeToString(out))
}

func getConfigDir(path string) string {
	return filepath.Join(path, "cfg")
}

func getConfigPath(path string) string {
	return filepath.Join(path, "cfg", "config.json")
}

func getLockFile(path string) string {
	return filepath.Join(path, "cfg", "lock")
}

func getStoreDir(path string) string {
	return filepath.Join(path, "store")
}

func getFilePath(path string, hash string) string {
	return filepath.Join(path, "store", hash)
}

func initFileCache(cache *FileCache, path string) {
	cache.dir = path
	cache.cullLock = make(chan struct{}, 1)
	cache.cullLock <- struct{}{}
}

// LoadFileCache loads a persistent cache from the file system.
func LoadFileCache(path string) (*FileCache, error) {
	var cache FileCache
	initFileCache(&cache, path)
	file, err := os.Open(getConfigPath(path))
	if err != nil {
		return nil, err
	}
	defer file.Close()
	dec := json.NewDecoder(file)
	if err = dec.Decode(&cache.cfg); err != nil {
		return nil, err
	}
	storeDir := getStoreDir(cache.dir)
	files, err := ioutil.ReadDir(storeDir)
	if err != nil {
		return nil, err
	}
	// curSize is a rough estimate of the cache size, not the actual true
	// cache size which can't be known at all times.
	cache.curSize = uint64(len(files))
	return &cache, nil
}

// NewFileCache creates a new persistent cache on the file system.
func NewFileCache(path string, size uint64) (*FileCache, error) {
	var cache FileCache
	initFileCache(&cache, path)
	cache.cfg.Size = size
	if err := os.MkdirAll(getConfigDir(path), os.ModePerm); err != nil {
		return nil, err
	}
	if err := os.MkdirAll(getStoreDir(path), os.ModePerm); err != nil {
		return nil, err
	}
	if err := os.MkdirAll(getTmpDir(path), os.ModePerm); err != nil {
		return nil, err
	}
	file, err := os.Create(getConfigPath(path))
	if err != nil {
		return nil, err
	}
	defer file.Close()
	enc := json.NewEncoder(file)
	if err = enc.Encode(cache.cfg); err != nil {
		return nil, err
	}
	return &cache, nil
}

// GetFileCache will attempt to load an existing cache if one exists.
// If one does not it will create a cache with the given size.
func GetFileCache(path string, size uint64) (*FileCache, error) {
	if cache, err := LoadFileCache(path); err == nil {
		return cache, nil
	}
	return NewFileCache(path, size)
}

// Update starts a goroutine to periodically check on the status of the cache. The
// cache can be operated without using Update but if Update is not called the cache's
// estimate of the cache size will diverge from reality. If many actors are present
// this can cause the cache size to be very different from this actor's estimate.
func (fc *FileCache) Update(ctx context.Context) {
	// Make sure we keep curSize more up to date. This avoids
	// us needing to limit the number of concurrent actors.
	go func() {
		storeDir := getStoreDir(fc.dir)
		for {
			select {
			case <-ctx.Done():
				return
			default:
			}
			// We don't want to be constantly hitting the file system with expensive
			// operations like ReadDir. This sleep ensures that we not be too aggressive.
			// By sleeping for 32 milliseconds we make the ReadDir call about 30 times a
			// second.
			time.Sleep(waitBeforeReadDir * time.Millisecond)
			files, err := ioutil.ReadDir(storeDir)
			if err == nil {
				atomic.StoreUint64(&fc.curSize, uint64(len(files)))
			}
		}
	}()
}

func (fc *FileCache) getFilePath(hash string) string {
	return getFilePath(fc.dir, hash)
}

// cullFiles obtains the lock file or exits. If the lock file is obtained
// then clean up occurs (slow). Other processes can read and get files while
// cleanup is occurring since all operations except culling are lock-free and
// atomic.
func (fc *FileCache) cullFiles() {
	// TODO(jakehehrlich): Add functionality to trigger a cull for total size as
	// well as number of files.

	// Acquire the cull lock so the same process doesn't try and create a file
	// too frequently.
	select {
	case <-fc.cullLock:
		// Make sure we release this process' cull lock when done.
		defer func() { fc.cullLock <- struct{}{} }()
	default:
		return
	}
	lockFile := getLockFile(fc.dir)
	file, err := os.OpenFile(lockFile, os.O_WRONLY|os.O_EXCL|os.O_CREATE, os.ModeExclusive)
	if err != nil {
		// Someone else is already cleaning so exit.
		return
	}
	// Make sure we release the lock when done.
	defer func() {
		name := file.Name()
		defer file.Close()
		os.Remove(name)
	}()
	// Now we know we have the lock and are the only process cleaning the cache
	// If the directory suddenly deleted or something else causes the ReadDir to
	// fail then we can set our count to zero. Some other operation will then
	// soon fail in a context where errors can be handled better.
	files, _ := ioutil.ReadDir(getStoreDir(fc.dir))
	// It could turn out we guessed wrong and no files need to be collected. Just
	// return in this case.
	numFiles := uint64(len(files))
	if numFiles <= fc.cfg.Size {
		// Update our count and return. One might be concerned
		// that this store might overwrite previous adds or that the collection
		// of operations do not look kosher or linerize in someway. This is not
		// an issue here as 'curSize' is just an estimate and all actors are
		// just trying to ensure that at any given time this value is as close
		// to the right value as possible. The value is just an estimate to
		// control how often a we try and collect garbage.
		atomic.StoreUint64(&fc.curSize, numFiles)
		return
	}
	// Now remove the oldest files first. Note that we update the mod time on
	// reads as a means of keeping hot files in the cache.
	sort.Slice(files, func(i, j int) bool {
		return files[j].ModTime().After(files[i].ModTime())
	})
	filesToRemove := numFiles - fc.cfg.Size
	for _, file := range files[:filesToRemove] {
		fpath := fc.getFilePath(file.Name())
		os.Remove(fpath)
	}
	// Finally update our estimate.
	atomic.StoreUint64(&fc.curSize, fc.cfg.Size)
}

// Add adds an entry into the cache so that it might later be accessed via
// Get.
func (fc *FileCache) Add(key FileKey, value io.Reader) (*LinkedFile, error) {
	file, err := os.Create(getTmpFile(fc.dir))
	if err != nil {
		return nil, err
	}
	defer func() {
		name := file.Name()
		file.Close()
		os.Remove(name)
	}()
	io.Copy(file, value)
	path := fc.getFilePath(key.Hash())
	// Link the file into the temporary directory for return.
	out := LinkedFile(getTmpFile(fc.dir))
	err = os.Link(file.Name(), out.String())
	if err != nil {
		return nil, err
	}
	// Rename the newly copied file into
	err = os.Rename(file.Name(), path)
	if err != nil {
		out.Close()
		return nil, err
	}
	// We never cull if the maximum size is zero.
	if fc.cfg.Size == 0 {
		return &out, nil
	}
	// We don't want to constantly try cleaning so we instead wait for our
	// estimate size to hit 1.25. This means that 4 processes can be using
	// the cache at the same time and the cache won't go over 2x size.
	if atomic.AddUint64(&fc.curSize, 1) >= fc.cfg.Size+fc.cfg.Size/4 {
		fc.cullFiles()
	}
	return &out, nil
}

// Get returns a LinkedFile to an entry for the given key if one exists.
func (fc *FileCache) Get(key FileKey) (*LinkedFile, error) {
	// We need to link the file to a temp file before returning the temp file.
	// This allows the file to be kept alive as long as the user needs while
	// the cache can be removed from freely.
	path := fc.getFilePath(key.Hash())
	tmpFile := getTmpFile(fc.dir)
	err := os.Link(path, tmpFile)
	if err != nil {
		return nil, err
	}
	// Update the ModTime so this file is less likely to be removed.
	t := time.Now()
	os.Chtimes(path, t, t)
	// If the file had not existed os.Link would have returned an error.
	lf := LinkedFile(tmpFile)
	return &lf, nil
}

// DeleteCache removes all traces of a cache located at cachepath.
func DeleteCache(cachepath string) error {
	tmpDir := getTmpDir(cachepath)
	storeDir := getStoreDir(cachepath)
	cfgDir := getConfigDir(cachepath)

	if err := os.RemoveAll(tmpDir); err != nil {
		return err
	}
	if err := os.RemoveAll(storeDir); err != nil {
		return err
	}
	if err := os.RemoveAll(cfgDir); err != nil {
		return err
	}
	if err := os.Remove(cachepath); err != nil {
		return err
	}
	return nil
}

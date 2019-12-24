// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package repo

import (
	"crypto/aes"
	"crypto/cipher"
	"crypto/rand"
	"crypto/sha512"
	"encoding/hex"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"io/ioutil"
	"os"
	"path/filepath"
	"time"

	"fuchsia.googlesource.com/merkle"
	"fuchsia.googlesource.com/pm/build"

	tuf "github.com/flynn/go-tuf"
)

var roles = []string{"timestamp", "targets", "snapshot"}

type ErrFileAddFailed string

func (e ErrFileAddFailed) Error() string {
	return fmt.Sprintf("file couldn't be added: %s", string(e))
}

func NewAddErr(m string, e error) ErrFileAddFailed {
	return ErrFileAddFailed(fmt.Sprintf("%s: %s", m, e))
}

type customTargetMetadata struct {
	Merkle string `json:"merkle"`
	Size   int64  `json:"size"`
}

// TimeProvider provides the service to get Unix timestamp.
type TimeProvider interface {
	// UnixTimestamp returns the Unix timestamp.
	UnixTimestamp() int
}

type Repo struct {
	*tuf.Repo
	path          string
	encryptionKey []byte
	timeProvider  TimeProvider
}

var NotCreatingNonExistentRepoError = errors.New("repo does not exist and createIfNotExist is false, so not creating one")

// SystemProvider uses the time pkg to get Unix timestamp.
type SystemTimeProvider struct{}

func (*SystemTimeProvider) UnixTimestamp() int {
	return int(time.Now().Unix())
}

func passphrase(role string, confirm bool) ([]byte, error) { return []byte{}, nil }

// New initializes a new Repo structure that may read/write repository data at
// the given path.
func New(path string) (*Repo, error) {
	if info, e := os.Stat(path); e != nil || !info.IsDir() {
		return nil, os.ErrInvalid
	}

	repo, err := tuf.NewRepo(tuf.FileSystemStore(path, passphrase), "sha512")
	if err != nil {
		return nil, err
	}
	r := &Repo{repo, path, nil, &SystemTimeProvider{}}

	blobDir := filepath.Join(r.path, "repository", "blobs")
	if err := os.MkdirAll(blobDir, os.ModePerm); err != nil {
		return nil, err
	}
	if err := os.MkdirAll(r.stagedFilesPath(), os.ModePerm); err != nil {
		return nil, err
	}

	return r, nil
}

func (r *Repo) EncryptWith(path string) error {
	keyBytes, err := ioutil.ReadFile(path)
	if err != nil {
		return err
	}
	r.encryptionKey = keyBytes
	return nil
}

// Init initializes a repository, reparing it for publishing. If a
// repository already exists, either os.ErrExist, or a TUF error are returned.
// If a repository does not exist at the given location, a repo will be created there.
func (r *Repo) Init() error {
	return r.OptionallyInitAtLocation(true)
}

// OptionallyInitAtLocation initializes a new repository, reparing it
// for publishing, if a repository does not already exist at its
// location and createIfNotExists is true.
// If a repository already exists, either os.ErrExist, or a TUF error are returned.
func (r *Repo) OptionallyInitAtLocation(createIfNotExists bool) error {
	if _, err := os.Stat(filepath.Join(r.path, "repository", "root.json")); err == nil {
		return os.ErrExist
	}

	// The repo doesn't exist at this location, but the caller may
	// want to treat this as an error.  The common case here is
	// that a command line user makes a typo and doesn't
	// understand why a new repo was silently created.
	if !createIfNotExists {
		return NotCreatingNonExistentRepoError
	}

	// Fuchsia repositories always use consistent snapshots.
	if err := r.Repo.Init(true); err != nil {
		return err
	}

	return nil
}

// GenKeys will generate a full suite of the necessary keys for signing a
// repository.
func (r *Repo) GenKeys() error {
	if _, err := r.GenKey("root"); err != nil {
		return err
	}
	for _, role := range roles {
		if _, err := r.GenKey(role); err != nil {
			return err
		}
	}
	return nil
}

// AddPackage adds a package with the given name with the content from the given
// reader. The package blob is also added. If merkle is non-empty, it is used,
// otherwise the package merkleroot is computed on the fly.
func (r *Repo) AddPackage(name string, rd io.Reader, merkle string) error {
	root, size, err := r.AddBlob(merkle, rd)
	if err != nil {
		return NewAddErr("adding package blob", err)
	}

	stagingPath := filepath.Join(r.stagedFilesPath(), name)
	os.MkdirAll(filepath.Dir(stagingPath), os.ModePerm)

	// add merkle root as custom JSON
	metadata := customTargetMetadata{Merkle: root, Size: size}
	jsonStr, err := json.Marshal(metadata)
	if err != nil {
		return NewAddErr(fmt.Sprintf("serializing %v", metadata), err)
	}

	blobDir := filepath.Join(r.path, "repository", "blobs")
	blobPath := filepath.Join(blobDir, root)

	if err := linkOrCopy(blobPath, stagingPath); err != nil {
		return NewAddErr("creating file in staging directory", err)
	}

	// add file with custom JSON to repository
	if err := r.AddTarget(name, json.RawMessage(jsonStr)); err != nil {
		return fmt.Errorf("failed adding target %s to TUF repo: %s", name, err)
	}

	return nil
}

func cryptingWriter(dst io.Writer, key []byte) (io.WriteCloser, error) {
	block, err := aes.NewCipher(key)
	if err != nil {
		return nil, err
	}
	iv := make([]byte, aes.BlockSize)
	// Write the iv to the front of the output stream
	if _, err := io.ReadFull(io.TeeReader(rand.Reader, dst), iv); err != nil {
		return nil, err
	}

	stream := cipher.NewCTR(block, iv)

	return cipher.StreamWriter{stream, dst, nil}, nil
}

// HasBlob returns true if the given merkleroot is already in the repository
// blob store.
func (r *Repo) HasBlob(root string) bool {
	blobPath := filepath.Join(r.path, "repository", "blobs", root)
	fi, err := os.Stat(blobPath)
	return err == nil && fi.Mode().IsRegular()
}

// AddBlob writes the content of the given reader to the blob identified by the
// given merkleroot. If merkleroot is empty string, a merkleroot is computed.
// Addblob always returns the plaintext size of the blob that is added, even if
// blob encryption is used.
func (r *Repo) AddBlob(root string, rd io.Reader) (string, int64, error) {
	blobDir := filepath.Join(r.path, "repository", "blobs")

	if root != "" {
		dstPath := filepath.Join(blobDir, root)
		if fi, err := os.Stat(dstPath); err == nil {
			fileSize := fi.Size()

			if r.encryptionKey != nil {
				fileSize -= aes.BlockSize
			}

			return root, fileSize, nil
		}

		var dst io.WriteCloser
		var err error
		dst, err = os.OpenFile(dstPath, os.O_CREATE|os.O_EXCL|os.O_WRONLY, 0666)
		if err != nil {
			return root, 0, err
		}
		defer dst.Close()
		if r.encryptionKey != nil {
			dst, err = cryptingWriter(dst, r.encryptionKey)
			if err != nil {
				return root, 0, err
			}
		}
		n, err := io.Copy(dst, rd)
		return root, n, err
	}

	var tree merkle.Tree
	f, err := ioutil.TempFile(blobDir, "blob")
	if err != nil {
		return "", 0, err
	}

	var dst io.WriteCloser = f
	if r.encryptionKey != nil {
		dst, err = cryptingWriter(dst, r.encryptionKey)
		if err != nil {
			return "", 0, err
		}
	}

	n, err := tree.ReadFrom(io.TeeReader(rd, dst))
	if err != nil {
		f.Close()
		return "", n, err
	}
	f.Close()
	root = hex.EncodeToString(tree.Root())
	return root, n, os.Rename(f.Name(), filepath.Join(blobDir, root))
}

// CommitUpdates finalizes the changes to the update repository that have been
// staged by calling AddPackageFile. Setting dateVersioning to true will set
// the version of the targets, snapshot, and timestamp metadata files based on
// an offset in seconds from epoch (1970-01-01 00:00:00 UTC).
func (r *Repo) CommitUpdates(dateVersioning bool) error {
	if dateVersioning {
		dTime := r.timeProvider.UnixTimestamp()
		tVer, err := r.TargetsVersion()
		if err != nil {
			return err
		}
		if dTime > tVer {
			r.SetTargetsVersion(dTime)
		}
		sVer, err := r.SnapshotVersion()
		if err != nil {
			return err
		}
		if dTime > sVer {
			r.SetSnapshotVersion(dTime)
		}
		tsVer, err := r.TimestampVersion()
		if err != nil {
			return err
		}
		if dTime > tsVer {
			r.SetTimestampVersion(dTime)
		}
	}
	return r.commitUpdates()
}

// PublishManifest publishes the package and blobs identified in the package
// output manifest at the given path, returning all input files involved, or an
// error.
func (r *Repo) PublishManifest(path string) ([]string, error) {
	deps := []string{path}
	f, err := os.Open(path)
	if err != nil {
		return nil, err
	}
	defer f.Close()
	var packageManifest build.PackageManifest
	if err := json.NewDecoder(f).Decode(&packageManifest); err != nil {
		return nil, err
	}
	if packageManifest.Version != "1" {
		return nil, fmt.Errorf("unknown version %q, can't publish", packageManifest.Version)
	}

	for _, blob := range packageManifest.Blobs {
		deps = append(deps, blob.SourcePath)
		if blob.Path == "meta/" {
			p := packageManifest.Package
			name := p.Name + "/" + p.Version
			f, err := os.Open(blob.SourcePath)
			if err != nil {
				return nil, err
			}
			err = r.AddPackage(name, f, blob.Merkle.String())
			f.Close()
		} else {
			if !r.HasBlob(blob.Merkle.String()) {
				f, err := os.Open(blob.SourcePath)
				if err != nil {
					return nil, err
				}
				_, _, err = r.AddBlob(blob.Merkle.String(), f)
				f.Close()
			}
		}
		if err != nil {
			return nil, err
		}
	}
	return deps, nil
}

func (r *Repo) commitUpdates() error {
	// TUF-1.0 section 4.4.2 states that the expiration must be in the
	// ISO-8601 format in the UTC timezone with no nanoseconds.
	expires := time.Now().AddDate(0, 0, 30).UTC().Round(time.Second)
	if err := r.SnapshotWithExpires(tuf.CompressionTypeNone, expires); err != nil {
		return fmt.Errorf("snapshot: %s", err)
	}
	if err := r.TimestampWithExpires(expires); err != nil {
		return fmt.Errorf("timestamp: %s", err)
	}
	if err := r.Commit(); err != nil {
		return fmt.Errorf("commit: %s", err)
	}

	return r.fixupRootConsistentSnapshot()
}

func (r *Repo) stagedFilesPath() string {
	return filepath.Join(r.path, "staged", "targets")
}

// when the repository is "pre-initialized" by a root.json from the build, but
// no root keys are available to the publishing step, the commit process does
// not produce a consistent snapshot file for the root json manifest. This
// method implements that production.
func (r *Repo) fixupRootConsistentSnapshot() error {
	b, err := ioutil.ReadFile(filepath.Join(r.path, "repository", "root.json"))
	if err != nil {
		return err
	}
	sum512 := sha512.Sum512(b)
	rootSnap := filepath.Join(r.path, "repository", fmt.Sprintf("%x.root.json", sum512))
	if _, err := os.Stat(rootSnap); os.IsNotExist(err) {
		if err := ioutil.WriteFile(rootSnap, b, 0666); err != nil {
			return err
		}
	}
	return nil
}

// link is available for stubbing in tests
var link = os.Link

func linkOrCopy(srcPath, dstPath string) error {
	if err := link(srcPath, dstPath); err != nil {
		s, err := os.Open(srcPath)
		if err != nil {
			return err
		}
		defer s.Close()

		d, err := ioutil.TempFile(filepath.Dir(dstPath), filepath.Base(dstPath))
		if err != nil {
			return err
		}
		if _, err := io.Copy(d, s); err != nil {
			d.Close()
			os.Remove(d.Name())
			return err
		}
		if err := d.Close(); err != nil {
			return err
		}
		return os.Rename(d.Name(), dstPath)
	}
	return nil
}

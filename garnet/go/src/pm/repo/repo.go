// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package repo

import (
	"crypto/sha512"
	"encoding/hex"
	"encoding/json"
	"fmt"
	"io"
	"io/ioutil"
	"os"
	"path/filepath"
	"time"

	"fuchsia.googlesource.com/merkle"

	tuf "github.com/flynn/go-tuf"
)

var roles = []string{"timestamp", "targets", "snapshot"}

type ErrFileAddFailed string

func (e ErrFileAddFailed) Error() string {
	return fmt.Sprintf("amber: file couldn't be added: %s", string(e))
}

func NewAddErr(m string, e error) ErrFileAddFailed {
	return ErrFileAddFailed(fmt.Sprintf("%s: %s", m, e))
}

type customTargetMetadata struct {
	Merkle string `json:"merkle"`
}

type Repo struct {
	*tuf.Repo
	path string
}

func passphrase(role string, confirm bool) ([]byte, error) { return []byte{}, nil }

// New initializes a new Repo structure that may read/write repository data at
// the given path.
func New(path string) (*Repo, error) {
	if info, e := os.Stat(path); e != nil || !info.IsDir() {
		return nil, os.ErrInvalid
	}

	repo, err := tuf.NewRepo(tuf.FileSystemStore(path, passphrase), "sha512")
	return &Repo{repo, path}, err
}

// Init initializes a new repository, preparing it for publishing. If a
// repository already exists, either os.ErrExist, or a TUF error are returned.
func (r *Repo) Init() error {
	if _, err := os.Stat(filepath.Join(r.path, "repository", "root.json")); err == nil {
		return os.ErrExist
	}

	// Fuchsia repositories always use consistent snapshots.
	return r.Repo.Init(true)
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

// Add a package with the given name with the content from the given reader. The
// package blob is also added.
func (r *Repo) AddPackage(name string, rd io.Reader) error {
	stagingPath := filepath.Join(r.stagedFilesPath(), name)
	os.MkdirAll(filepath.Dir(stagingPath), os.ModePerm)

	dst, err := os.Create(stagingPath)
	if err != nil {
		return NewAddErr("creating file in staging directory", err)
	}
	root, err := r.AddBlob("", io.TeeReader(rd, dst))
	dst.Close()
	if err != nil {
		return NewAddErr("adding package blob", err)
	}

	// add merkle root as custom JSON
	metadata := customTargetMetadata{Merkle: root}
	jsonStr, err := json.Marshal(metadata)
	if err != nil {
		return NewAddErr(fmt.Sprintf("serializing %v", metadata), err)
	}

	// add file with custom JSON to repository
	if err := r.AddTarget(name, json.RawMessage(jsonStr)); err != nil {
		return fmt.Errorf("failed adding target %s to TUF repo: %s", name, err)
	}

	return nil
}

// AddBlob writes the content of the given reader to the blob identified by the
// given merkleroot. If merkleroot is empty string, a merkleroot is computed.
func (r *Repo) AddBlob(root string, rd io.Reader) (string, error) {
	blobDir := filepath.Join(r.path, "repository", "blobs")
	os.MkdirAll(blobDir, os.ModePerm)

	if root != "" {
		dst := filepath.Join(blobDir, root)
		f, err := os.OpenFile(dst, os.O_CREATE|os.O_EXCL|os.O_WRONLY, 0666)
		if err != nil {
			if os.IsExist(err) {
				return root, nil
			}
			return root, err
		}
		defer f.Close()
		_, err = io.Copy(f, rd)
		return root, err
	}

	var tree merkle.Tree
	f, err := ioutil.TempFile(blobDir, "blob")
	if err != nil {
		return "", err
	}
	if _, err := tree.ReadFrom(io.TeeReader(rd, f)); err != nil {
		f.Close()
		return "", err
	}
	f.Close()
	root = hex.EncodeToString(tree.Root())
	return root, os.Rename(f.Name(), filepath.Join(blobDir, root))
}

// CommitUpdates finalizes the changes to the update repository that have been
// staged by calling AddPackageFile. Setting dateVersioning to true will set
// the version of the targets, snapshot, and timestamp metadata files based on
// an offset in seconds from epoch (1970-01-01 00:00:00 UTC).
func (r *Repo) CommitUpdates(dateVersioning bool) error {
	if dateVersioning {
		dTime := int(time.Now().Unix())

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

func (r *Repo) commitUpdates() error {
	if err := r.SnapshotWithExpires(tuf.CompressionTypeNone, time.Now().AddDate(0, 0, 30)); err != nil {
		return NewAddErr("problem snapshotting repository", err)
	}
	if err := r.TimestampWithExpires(time.Now().AddDate(0, 0, 30)); err != nil {
		return NewAddErr("problem timestamping repository", err)
	}
	if err := r.Commit(); err != nil {
		return NewAddErr("problem committing repository changes", err)
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
		return ioutil.WriteFile(rootSnap, b, 0666)
	}
	return nil
}

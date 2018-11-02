// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package publish

import (
	"encoding/hex"
	"encoding/json"
	"fmt"
	"io"
	"io/ioutil"
	"os"
	"path"
	"path/filepath"
	"time"

	"fuchsia.googlesource.com/merkle"

	tuf "github.com/flynn/go-tuf"
)

var keySet = []string{"timestamp.json", "targets.json", "snapshot.json"}

const rootJSONName = "root_manifest.json"

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

type UpdateRepo struct {
	repo *tuf.Repo
	path string
}

func InitRepo(r string, k string) (*UpdateRepo, error) {
	if info, e := os.Stat(r); e != nil || !info.IsDir() {
		return nil, os.ErrInvalid
	}

	keysDir := filepath.Join(r, "keys")
	if e := os.MkdirAll(keysDir, os.ModePerm); e != nil {
		return nil, e
	}

	// If we're copying pre-made keys from some source, then we need to do that
	// before initializing the repositroy.
	if k != keysDir {
		if e := populateKeys(keysDir, k); e != nil {
			return nil, e
		}

		rootOut := filepath.Join(r, "repository", "root.json")
		if e := copyFile(rootOut, filepath.Join(k, rootJSONName)); e != nil {
			return nil, e
		}
	}

	s := tuf.FileSystemStore(r, func(role string, confirm bool) ([]byte, error) { return []byte{}, nil })
	repo, e := tuf.NewRepo(s, "sha512")
	if e != nil {
		return nil, e
	}

	// If we're constructing the roles ourself, we use the repo initialized above.
	if k == keysDir {
		for _, role := range []string{"root", "targets", "snapshot", "timestamp"} {
			if ss, err := s.GetSigningKeys(role); err != nil || len(ss) == 0 {
				_, err := repo.GenKey(role)
				if err != nil {
					return nil, err
				}
			}
		}
	}

	u := &UpdateRepo{repo: repo, path: r}

	return u, nil
}

// Add a package with the given name with the content from the given reader. The
// package blob is also added.
func (u *UpdateRepo) AddPackage(name string, r io.Reader) error {
	stagingPath := filepath.Join(u.stagedFilesPath(), name)
	os.MkdirAll(filepath.Dir(stagingPath), os.ModePerm)

	dst, err := os.Create(stagingPath)
	if err != nil {
		return NewAddErr("creating file in staging directory", err)
	}
	root, err := u.AddBlob("", io.TeeReader(r, dst))
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
	if err := u.repo.AddTarget(name, json.RawMessage(jsonStr)); err != nil {
		return fmt.Errorf("failed adding target %s to TUF repo: %s", name, err)
	}

	return nil
}

func (u *UpdateRepo) AbortStaged() error {
	return u.repo.Clean()
}

// AddBlob writes the content of the given reader to the blob identified by the
// given merkleroot. If merkleroot is empty string, a merkleroot is computed.
func (u *UpdateRepo) AddBlob(root string, r io.Reader) (string, error) {
	blobDir := filepath.Join(u.path, "repository", "blobs")
	os.MkdirAll(blobDir, os.ModePerm)

	if root != "" {
		dst := filepath.Join(blobDir, root)
		f, err := os.OpenFile(dst, os.O_CREATE|os.O_EXCL|os.O_WRONLY, os.ModePerm)
		if err != nil {
			return root, err
		}
		defer f.Close()
		_, err = io.Copy(f, r)
		return root, err
	}

	var tree merkle.Tree
	f, err := ioutil.TempFile(blobDir, "blob")
	if err != nil {
		return "", err
	}
	if _, err := tree.ReadFrom(io.TeeReader(r, f)); err != nil {
		f.Close()
		return "", err
	}
	f.Close()
	root = hex.EncodeToString(tree.Root())
	return root, os.Rename(f.Name(), filepath.Join(blobDir, root))
}

func (u *UpdateRepo) RemoveContentBlob(merkle string) error {
	return os.Remove(filepath.Join(u.path, "repository", "blobs", merkle))
}

// CommitUpdates finalizes the changes to the update repository that have been
// staged by calling AddPackageFile. Setting dateVersioning to true will set
// the version of the targets, snapshot, and timestamp metadata files based on
// an offset in seconds from epoch (1970-01-01 00:00:00 UTC).
func (u *UpdateRepo) CommitUpdates(dateVersioning bool) error {
	if dateVersioning {
		dTime := int(time.Now().Unix())

		tVer, err := u.repo.TargetsVersion()
		if err != nil {
			return err
		}
		if dTime > tVer {
			u.repo.SetTargetsVersion(dTime)
		}

		sVer, err := u.repo.SnapshotVersion()
		if err != nil {
			return err
		}
		if dTime > sVer {
			u.repo.SetSnapshotVersion(dTime)
		}

		tsVer, err := u.repo.TimestampVersion()
		if err != nil {
			return err
		}
		if dTime > tsVer {
			u.repo.SetTimestampVersion(dTime)
		}
	}
	return u.commitUpdates()
}

func (u *UpdateRepo) commitUpdates() error {
	if err := u.repo.SnapshotWithExpires(tuf.CompressionTypeNone, time.Now().AddDate(0, 0, 30)); err != nil {
		return NewAddErr("problem snapshotting repository", err)
	}
	if err := u.repo.TimestampWithExpires(time.Now().AddDate(0, 0, 30)); err != nil {
		return NewAddErr("problem timestamping repository", err)
	}
	if err := u.repo.Commit(); err != nil {
		return NewAddErr("problem committing repository changes", err)
	}

	return nil
}

func (u *UpdateRepo) stagedFilesPath() string {
	return filepath.Join(u.path, "staged", "targets")
}

func copyFile(dst string, src string) error {
	info, err := os.Stat(src)
	if err != nil {
		return err
	}

	if !info.Mode().IsRegular() {
		return os.ErrInvalid
	}

	os.MkdirAll(path.Dir(dst), os.ModePerm)

	in, err := os.Open(src)
	if err != nil {
		return err
	}
	defer in.Close()

	out, err := os.Create(dst)
	if err != nil {
		return err
	}
	defer out.Close()

	_, err = io.Copy(out, in)

	return err
}

// populateKeys copies the keys necessary for publishing from a source path.
// Note that we don't copy a root key anywhere. The root key is only needed for
// signing these keys and we assume it is maintained outside the repo somewhere.
func populateKeys(destPath string, srcPath string) error {
	for _, k := range keySet {
		if err := copyFile(filepath.Join(destPath, k), filepath.Join(srcPath, k)); err != nil {
			return err
		}
	}

	return nil
}

func computeMerkle(path string) ([]byte, error) {
	f, err := os.Open(path)
	if err != nil {
		return nil, err
	}
	mTree := &merkle.Tree{}

	if _, err = mTree.ReadFrom(f); err != nil {
		return nil, err
	}
	return mTree.Root(), nil
}

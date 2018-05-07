// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package publish

import (
	"encoding/hex"
	"encoding/json"
	"fmt"
	"io"
	"os"
	"path"
	"path/filepath"
	"time"

	"fuchsia.googlesource.com/merkle"

	tuf "github.com/flynn/go-tuf"
)

var keySet = [3]string{"timestamp.json", "targets.json", "snapshot.json"}

const rootJSONName = "root_manifest.json"

type ErrFileAddFailed string

func (e ErrFileAddFailed) Error() string {
	return fmt.Sprintf("amber: file couldn't be added: %s", string(e))
}

func NewAddErr(m string, e error) ErrFileAddFailed {
	return ErrFileAddFailed(fmt.Sprintf("%s: %s", m, e))
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
	if e := os.MkdirAll(keysDir, 0777); e != nil {
		return nil, e
	}

	if e := populateKeys(keysDir, k); e != nil {
		return nil, e
	}

	if e := copyFile(filepath.Join(r, "repository", "root.json"), filepath.Join(k, rootJSONName)); e != nil {
		fmt.Println("Failed to copy root manifest")
		return nil, e
	}

	s := tuf.FileSystemStore(r, func(role string, confirm bool) ([]byte, error) { return []byte(""), nil })
	repo, e := tuf.NewRepo(s, "sha512")
	if e != nil {
		return nil, e
	}

	u := &UpdateRepo{repo: repo, path: r}

	// do a commit of the empty repository so that we are
	// always have a valid repository
	if e := os.MkdirAll(u.stagedFilesPath(), os.ModePerm); e != nil {
		return nil, e
	}

	if e := repo.AddTargets([]string{}, nil); e != nil {
		return nil, e
	}

	if e := u.CommitUpdates(false); e != nil {
		return nil, e
	}

	return u, nil
}

func (u *UpdateRepo) AddPackageFile(src string, name string) error {
	stagingPath := filepath.Join(u.stagedFilesPath(), name)

	if err := copyFile(stagingPath, src); err != nil {
		return NewAddErr("copying file to staging directory failed", err)
	}
	if err := u.createTUFMeta(stagingPath, name); err != nil {
		return NewAddErr("problem creating TUF metadata", err)
	}

	return nil
}

func (u *UpdateRepo) AbortStaged() error {
	return u.repo.Clean()
}

// AddContentBlob adds the blob specified by src to the repository. The blob is
// named according to its Merkle root. Upon success the Merkle root is returned
// as a string. If the blob already exists the error return value is set to
// os.ErrExist and the Merkle root is valid. If an error occurs computing the
// Merkle root, the returned Merkle root will be invalid and the error value
// is set. If an error happens while copying the file that error is returned
// and the Merkle root is valid.
func (u *UpdateRepo) AddContentBlob(src string) (string, error) {
	root, err := computeMerkle(src)
	if err != nil {
		return "", err
	}

	rootStr := hex.EncodeToString(root)
	return u.AddContentBlobWithMerkle(src, rootStr)
}

// AddContentBlobWithMerkle adds the blob specified by src with the precomputed
// merkle root `merkle` to the repository.
func (u *UpdateRepo) AddContentBlobWithMerkle(src, merkle string) (string, error) {
	dst := filepath.Join(u.path, "repository", "blobs", merkle)
	if _, err := os.Stat(dst); err == nil {
		return merkle, os.ErrExist
	}
	return merkle, copyFile(dst, src)
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

func (u *UpdateRepo) createTUFMeta(path string, name string) error {
	// compute merkle root
	root, err := computeMerkle(path)
	if err != nil {
		return fmt.Errorf("merkle computation failed: %s", err)
	}

	// add merkle root as custom JSON
	jsonStr := fmt.Sprintf("{\"merkle\":\"%x\"}", root)
	json := json.RawMessage(jsonStr)

	// add file with custom JSON to repository
	if err := u.repo.AddTarget(name, json); err != nil {
		return fmt.Errorf("failed adding target to TUF repo: %s", err)
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

	os.MkdirAll(path.Dir(dst), 0777)

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

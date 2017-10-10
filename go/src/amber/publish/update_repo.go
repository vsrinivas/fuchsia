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

	"fuchsia.googlesource.com/pm/merkle"

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

	return &UpdateRepo{repo: repo, path: r}, nil
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
	dst := filepath.Join(u.path, "repository", "blobs", rootStr)
	if _, err = os.Stat(dst); err == nil {
		return rootStr, os.ErrExist
	}

	return rootStr, copyFile(dst, src)
}

func (u *UpdateRepo) RemoveContentBlob(merkle string) error {
	return os.Remove(filepath.Join(u.path, "repository", "blobs", merkle))
}

func (u *UpdateRepo) CommitUpdates() error {
	if err := u.repo.Snapshot(tuf.CompressionTypeNone); err != nil {
		return NewAddErr("problem snapshotting repository", err)
	}
	if err := u.repo.TimestampWithExpires(time.Now().AddDate(0, 0, 7)); err != nil {
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
		return err
	}

	// add merkle root as custom JSON
	jsonStr := fmt.Sprintf("{\"merkle\":\"%x\"}", root)
	json := json.RawMessage(jsonStr)

	// add file with custom JSON to repository
	return u.repo.AddTarget(name, json)
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

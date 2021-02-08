// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package osmisc

import (
	"errors"
	"fmt"
	"io/ioutil"
	"os"
	"path/filepath"
)

// IsDir determines whether a given path exists *and* is a directory. It will
// return false (with no error) if the path does not exist. It will return true
// if the path exists, even if the user doesn't have permission to enter and
// read files in the directory.
func IsDir(path string) (bool, error) {
	info, err := os.Stat(path)
	if errors.Is(err, os.ErrNotExist) {
		return false, nil
	} else if err != nil {
		return false, err
	}
	return info.IsDir(), nil
}

// DirIsEmpty returns whether a given directory is empty.
// By convention, we say that a directory is empty if it does not exist.
func DirIsEmpty(dir string) (bool, error) {
	entries, err := ioutil.ReadDir(dir)
	if errors.Is(err, os.ErrNotExist) {
		return true, nil
	} else if err != nil {
		return false, err
	}
	return len(entries) == 0, nil
}

// CopyDir copies the src directory into the target directory, preserving file
// and directory modes.
func CopyDir(srcDir, dstDir string) error {
	err := filepath.Walk(srcDir, func(srcPath string, info os.FileInfo, err error) error {
		if err != nil {
			return err
		}

		relPath, err := filepath.Rel(srcDir, srcPath)
		if err != nil {
			return err
		}

		dstPath := filepath.Join(dstDir, relPath)

		switch info.Mode() & os.ModeType {
		case 0: // default file
			if err := CopyFile(srcPath, dstPath); err != nil {
				return err
			}

		case os.ModeDir:
			if err := os.Mkdir(dstPath, info.Mode()); err != nil && !os.IsExist(err) {
				return err
			}

		case os.ModeSymlink:
			srcLink, err := os.Readlink(srcPath)
			if err != nil {
				return err
			}
			if err := os.Symlink(srcLink, dstPath); err != nil {
				return err
			}

		default:
			return fmt.Errorf("unknown file type for %s", srcPath)
		}

		return nil
	})

	return err
}

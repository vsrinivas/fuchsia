// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package osmisc

import (
	"fmt"
	"io"
	"os"
	"path/filepath"
)

// CopyFile copies a file to a given destination.
func CopyFile(src, dest string) error {
	in, err := os.Open(src)
	if err != nil {
		return err
	}
	defer in.Close()
	info, err := in.Stat()
	if err != nil {
		return err
	}
	if err := os.MkdirAll(filepath.Dir(dest), os.ModePerm); err != nil {
		return fmt.Errorf("failed to make parent dirs of %s: %w", dest, err)
	}
	out, err := os.OpenFile(dest, os.O_WRONLY|os.O_CREATE, info.Mode().Perm())
	if err != nil {
		return err
	}
	defer out.Close()
	_, err = io.Copy(out, in)
	return err
}

// FileIsOpen returns whether the given file is open or not.
func FileIsOpen(f *os.File) bool {
	return f.Fd() != ^uintptr(0)
}

// CreateFile creates the file specified by the given path and all parent directories if they don't exist.
// If flags are provided, they will be passed to open() with os.O_CREATE.
func CreateFile(path string, flags ...int) (*os.File, error) {
	if err := os.MkdirAll(filepath.Dir(path), os.ModePerm); err != nil {
		return nil, fmt.Errorf("failed to make parent dirs of %s: %w", path, err)
	}
	if len(flags) == 0 {
		return os.Create(path)
	}
	flagSet := os.O_CREATE
	for _, flag := range flags {
		flagSet = flagSet | flag
	}
	return os.OpenFile(path, flagSet, os.ModePerm)
}

// FileExists returns whether a given file exists.
func FileExists(name string) (bool, error) {
	fi, err := os.Stat(name)
	if os.IsNotExist(err) {
		return false, nil
	} else if err != nil {
		return false, err
	}
	if fi.IsDir() {
		return false, fmt.Errorf("%s is directory, not a file", name)
	}
	return true, nil
}

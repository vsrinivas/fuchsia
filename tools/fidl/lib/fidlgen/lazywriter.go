// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package fidlgen

import (
	"bytes"
	"io/ioutil"
	"os"
	"path/filepath"
)

type LazyWriter struct {
	destination string
	temp        *os.File
}

func NewLazyWriter(destination string) (*LazyWriter, error) {
	dest_dir := filepath.Dir(destination)
	err := os.MkdirAll(dest_dir, 0755)
	if err != nil {
		return nil, err
	}
	temp, err := ioutil.TempFile(dest_dir, filepath.Base(destination)+".*")
	if err != nil {
		return nil, err
	}
	return &LazyWriter{destination: destination, temp: temp}, nil
}

func (lw *LazyWriter) Write(p []byte) (n int, err error) {
	return lw.temp.Write(p)
}

func (lw *LazyWriter) Close() error {
	temp_name := lw.temp.Name()
	// always clean up the temporary file
	defer os.Remove(temp_name)
	err := lw.temp.Close()
	if err != nil {
		return err
	}

	temp_stat, err := os.Stat(temp_name)
	if err != nil {
		return err
	}
	dest_stat, err := os.Stat(lw.destination)
	if os.IsNotExist(err) {
		// destination doesn't exist yet
		return os.Rename(temp_name, lw.destination)
	}
	if err != nil {
		return err
	}
	if temp_stat.Size() != dest_stat.Size() {
		// sizes don't match, replace it
		return os.Rename(temp_name, lw.destination)
	}

	temp, err := os.ReadFile(temp_name)
	if err != nil {
		return err
	}
	dest, err := os.ReadFile(lw.destination)
	if err != nil {
		return err
	}

	if !bytes.Equal(temp, dest) {
		// contents don't match, replace
		return os.Rename(temp_name, lw.destination)
	}

	return nil
}

// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package fidlgen

import (
	"bytes"
	"os"
	"path/filepath"
)

// WriteFileIfChanged overwrite the filename with new contents unless the file already
// has those contents.
func WriteFileIfChanged(filename string, contents []byte) error {
	var current []byte
	stat, err := os.Stat(filename)
	if os.IsNotExist(err) {
		goto overwrite
	}
	if err != nil {
		return err
	}
	if stat.Size() != int64(len(contents)) {
		goto overwrite
	}
	current, err = os.ReadFile(filename)
	if err != nil {
		return err
	}
	if bytes.Compare(current, contents) == 0 {
		// Contents match
		return nil
	}

overwrite:
	if err := os.MkdirAll(filepath.Dir(filename), os.FileMode(0777)); err != nil {
		return err
	}
	return os.WriteFile(filename, contents, os.FileMode(0666))
}

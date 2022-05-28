// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"context"
	"path/filepath"
	"testing"

	"go.fuchsia.dev/fuchsia/tools/artifactory"
)

func TestFilterNonExistentFiles(t *testing.T) {
	t.Run("non-existent sources are skipped", func(t *testing.T) {
		dir := t.TempDir()
		ctx := context.Background()
		nonexistentFile := artifactory.Upload{Source: filepath.Join(dir, "nonexistent")}
		files, err := filterNonExistentFiles(ctx, []artifactory.Upload{nonexistentFile})
		if err != nil {
			t.Fatal(err)
		}
		if len(files) > 0 {
			t.Fatal("filtered files should be empty")
		}
	})
}

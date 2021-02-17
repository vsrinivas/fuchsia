// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package artifacts

import (
	"context"
	"io/ioutil"
	"os"
	"path/filepath"
	"strings"
	"testing"
)

func TestDownload(t *testing.T) {
	tmpDir := t.TempDir()
	// The expected command will be something like `artifacts cp -build <id> -src <src> -dst <dst> -srcs-file <srcs-file>`.
	// Make a mock script to copy src to dst. If src doesn't exist, create it so that a retry of the command will succeed.
	mockArtifactsScript := filepath.Join(tmpDir, "artifacts")
	if err := ioutil.WriteFile(mockArtifactsScript, []byte("#!/bin/bash\ncp -r $5 $7 || (echo contents > $5 && exit 1)"), os.ModePerm); err != nil {
		t.Fatal(err)
	}
	mockArtifactsWithSrcsFileScript := filepath.Join(tmpDir, "artifacts_retry_srcsfile")
	if err := ioutil.WriteFile(mockArtifactsWithSrcsFileScript, []byte("#!/bin/bash\nmkdir $7;srcs=$(cat $9);cp $srcs $7 || (for src in $srcs; do echo contents > $src; done && exit 1)"), os.ModePerm); err != nil {
		t.Fatal(err)
	}
	srcDir := filepath.Join(tmpDir, "src_dir")
	if err := os.Mkdir(srcDir, os.ModePerm); err != nil {
		t.Fatal(err)
	}
	srcFile := filepath.Join(srcDir, "src_file")
	if err := ioutil.WriteFile(srcFile, []byte("src"), os.ModePerm); err != nil {
		t.Fatal(err)
	}

	tests := []struct {
		name          string
		srcs          []string
		dst           string
		expectedFiles map[string]string
		forceRetry    bool
	}{
		{
			name:          "download dir",
			srcs:          []string{srcDir},
			dst:           "dst_dir",
			expectedFiles: map[string]string{filepath.Join("dst_dir", "src_file"): "src"},
		},
		{
			name:          "download file",
			srcs:          []string{srcFile},
			dst:           "dst_file",
			expectedFiles: map[string]string{"dst_file": "src"},
		},
		{
			name:          "retry download",
			srcs:          []string{filepath.Join(tmpDir, "new_src")},
			dst:           "dst_file",
			expectedFiles: map[string]string{"dst_file": "contents"},
			forceRetry:    true,
		},
		{
			name:          "retry download with srcs-file",
			srcs:          []string{filepath.Join(tmpDir, "new_src"), filepath.Join(tmpDir, "new_src2")},
			dst:           "",
			expectedFiles: map[string]string{"new_src": "contents", "new_src2": "contents"},
			forceRetry:    true,
		},
	}

	for _, test := range tests {
		t.Run(test.name, func(t *testing.T) {
			artifactsPath := mockArtifactsScript
			if len(test.srcs) > 1 {
				artifactsPath = mockArtifactsWithSrcsFileScript
			}
			if test.forceRetry {
				// Make sure all srcs don't exist so that the script fails the first time.
				for _, src := range test.srcs {
					os.Remove(src)
				}
			}
			archive := NewArchive("", artifactsPath)
			tmpDstDir := filepath.Join(tmpDir, "test")
			if err := os.Mkdir(tmpDstDir, os.ModePerm); err != nil {
				t.Fatal(err)
			}
			defer os.RemoveAll(tmpDstDir)
			if err := archive.download(context.Background(), "id", false, filepath.Join(tmpDstDir, test.dst), test.srcs); err != nil {
				t.Fatal(err)
			}
			for expectedFile, expectedContents := range test.expectedFiles {
				data, err := ioutil.ReadFile(filepath.Join(tmpDstDir, expectedFile))
				if err != nil {
					t.Error(err)
				}
				if strings.Trim(string(data), "\n") != expectedContents {
					t.Errorf("expected contents: %s, got: %s", expectedContents, data)
				}
			}
		})
	}
}

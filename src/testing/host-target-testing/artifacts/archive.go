// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package artifacts

import (
	"bufio"
	"bytes"
	"context"
	"fmt"
	"io/ioutil"
	"os"
	"path/filepath"
	"strings"
	"time"

	"golang.org/x/crypto/ssh"

	"go.fuchsia.dev/fuchsia/src/testing/host-target-testing/util"
	"go.fuchsia.dev/fuchsia/tools/lib/logger"
	"go.fuchsia.dev/fuchsia/tools/lib/retry"
)

// Archive allows interacting with the build artifact repository.
type Archive struct {
	// lkg (typically found in $FUCHSIA_DIR/prebuilt/tools/lkg/lkg) is
	// used to look up the latest build id for a given builder.
	lkgPath string

	// artifacts (typically found in $FUCHSIA_DIR/prebuilt/tools/artifacts/artifacts)
	// is used to download artifacts for a given build id.
	artifactsPath string
}

// NewArchive creates a new Archive.
func NewArchive(lkgPath string, artifactsPath string) *Archive {
	return &Archive{
		lkgPath:       lkgPath,
		artifactsPath: artifactsPath,
	}
}

// GetBuilder returns a Builder with the given name and Archive.
func (a *Archive) GetBuilder(name string) *Builder {
	return &Builder{archive: a, name: name}
}

// GetBuildByID returns an ArtifactsBuild for fetching artifacts for the build
// with the given id.
func (a *Archive) GetBuildByID(
	ctx context.Context,
	id string,
	dir string,
	publicKey ssh.PublicKey,
) (*ArtifactsBuild, error) {
	// Make sure the build exists.
	srcs, err := a.list(ctx, id)
	if err != nil {
		return nil, err
	}

	srcsMap := make(map[string]struct{})
	for _, src := range srcs {
		srcsMap[src] = struct{}{}
	}

	return &ArtifactsBuild{
		id:           id,
		archive:      a,
		dir:          dir,
		sshPublicKey: publicKey,
		srcs:         srcsMap,
	}, nil
}

// list artifacts that make up a build id `buildID`.
func (a *Archive) list(ctx context.Context, buildID string) ([]string, error) {
	args := []string{"ls", "-build", buildID}
	stdout, stderr, err := util.RunCommand(ctx, a.artifactsPath, args...)
	if err != nil {
		if len(stderr) != 0 {
			fmt.Printf("artifacts output: \n%s", stdout)
			return nil, fmt.Errorf("artifacts failed: %w: %s", err, string(stderr))
		}
		return nil, fmt.Errorf("artifacts failed: %w", err)
	}

	var lines []string
	sc := bufio.NewScanner(bytes.NewReader(stdout))
	for sc.Scan() {
		lines = append(lines, sc.Text())
	}

	return lines, nil
}

// Download artifacts from the build id `buildID` and write them to `dst`.
// If `srcs` contains only one source, it will copy the file or directory
// directly to `dst`. Otherwise, `dst` should be the directory under which to
// download the artifacts.
func (a *Archive) download(ctx context.Context, buildID string, fromRoot bool, dst string, srcs []string) error {
	tmpDir, err := ioutil.TempDir("", "download")
	if err != nil {
		return err
	}
	defer os.RemoveAll(tmpDir)

	// Filter out any duplicate sources.
	srcs = removeDuplicates(srcs)

	var src string
	var srcsFile string
	if len(srcs) > 1 {
		var filesToDownload []string
		var filesToSkip []string
		for _, src := range srcs {
			path := filepath.Join(dst, src)

			// We only need to download the file if it doesn't
			// exist locally, or the local path is a directory
			// (since we don't know what files exist in a directory
			// ahead of time).
			if st, err := os.Stat(path); err != nil || st.IsDir() {
				filesToDownload = append(filesToDownload, src)
			} else {
				filesToSkip = append(filesToSkip, src)
			}
		}

		logger.Infof(ctx, "skipping %d files to download", len(filesToSkip))
		if len(filesToDownload) == 0 {
			// Skip downloading if the files are already present in the build dir.
			logger.Infof(ctx, "no files to download")
			return nil
		}

		tmpFile, err := ioutil.TempFile(tmpDir, "srcs-file")
		if err != nil {
			return err
		}
		tmpFile.Close()
		srcsFile = tmpFile.Name()
		if err := ioutil.WriteFile(srcsFile, []byte(strings.Join(filesToDownload, "\n")), 0755); err != nil {
			return fmt.Errorf("failed to write srcs-file: %w", err)
		}
	} else {
		if st, err := os.Stat(dst); err == nil && !st.IsDir() {
			// Skip downloading if the file is already present in the build dir.
			return nil
		}
		src = srcs[0]
	}

	logger.Infof(ctx, "downloading %d artifacts to %s", len(srcs), dst)

	// The `artifacts` utility can occasionally run into transient issues. This implements a retry policy
	// that attempts to avoid such issues causing flakes.
	eb := retry.NewExponentialBackoff(100*time.Millisecond, 10*time.Second, 2)
	// ~12 seconds to hit backoff ceiling; 2.5 minutes of slack (given the above EB)
	retryCap := uint64(22)
	return retry.Retry(ctx, retry.WithMaxAttempts(eb, retryCap), func() error {
		// We don't want to leak files if we are interrupted during a download.
		// So we'll download all files to a temporary directory before moving
		// them to the specified destination, and we'll remove them in the case
		// of an error.
		tmpDst := filepath.Join(tmpDir, filepath.Base(dst))
		defer os.RemoveAll(tmpDst)
		args := []string{
			"cp",
			"-build", buildID,
			"-src", src,
			"-dst", tmpDst,
		}

		if fromRoot {
			args = append(args, "-root")
		}
		if srcsFile != "" {
			args = append(args, "-srcs-file", srcsFile)
		}

		stdout, stderr, err := util.RunCommand(ctx, a.artifactsPath, args...)
		if len(stdout) != 0 {
			logger.Infof(ctx, "artifacts stdout:\n%s", stdout)
		}
		if err != nil {
			if len(stderr) != 0 {
				logger.Infof(ctx, "artifacts stderr:\n%s", stderr)
				return fmt.Errorf("artifacts failed: %w: %s", err, string(stderr))
			}
			return fmt.Errorf("artifacts failed: %w", err)
		}
		return filepath.Walk(tmpDst, func(path string, info os.FileInfo, err error) error {
			if err != nil {
				return err
			}
			if path == tmpDst && info.IsDir() {
				return nil
			}
			relPath, err := filepath.Rel(tmpDst, path)
			if err != nil {
				return err
			}
			dstPath := filepath.Join(dst, relPath)
			if fi, err := os.Stat(dstPath); err == nil {
				if fi.IsDir() {
					// If dstPath already exists and is a directory, then return nil so
					// we can walk the contents of the directory and move over the
					// individual files.
					return nil
				}
			}
			if err = os.MkdirAll(filepath.Dir(dstPath), os.ModePerm); err != nil {
				return err
			}
			if info.IsDir() {
				// Move/replace entire directory and skip walking contents.
				err = filepath.SkipDir
				os.RemoveAll(dstPath)
			}
			if moveErr := os.Rename(path, dstPath); moveErr != nil {
				return moveErr
			}
			return err
		})
	}, nil)
}

// removeDuplicates removes any duplicated items in the list.
func removeDuplicates(srcs []string) []string {
	var srcList []string
	seen := make(map[string]struct{})
	for _, src := range srcs {
		if _, ok := seen[src]; !ok {
			srcList = append(srcList, src)
			seen[src] = struct{}{}
		}
	}
	return srcList
}

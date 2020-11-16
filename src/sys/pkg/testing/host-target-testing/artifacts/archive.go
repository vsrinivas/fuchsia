// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package artifacts

import (
	"context"
	"fmt"
	"io/ioutil"
	"os"
	"path/filepath"
	"strings"

	"golang.org/x/crypto/ssh"

	"go.fuchsia.dev/fuchsia/src/sys/pkg/testing/host-target-testing/util"
	"go.fuchsia.dev/fuchsia/tools/lib/logger"
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
	args := []string{"ls", "-build", id}
	stdout, stderr, err := util.RunCommand(ctx, a.artifactsPath, args...)
	if err != nil {
		if len(stderr) != 0 {
			fmt.Printf("artifacts output: \n%s", stdout)
			return nil, fmt.Errorf("artifacts failed: %w: %s", err, string(stderr))
		}
		return nil, fmt.Errorf("artifacts failed: %w", err)
	}

	// TODO(fxbug.dev/60451): While we are still looking up artifacts from builds
	// that have expired artifacts or do not include all_blobs.json, we must
	// retrieve the archives instead. Remove when we no longer need to fetch from
	// old builds that don't have the proper artifacts present in the artifacts
	// bucket.
	backupArchiveBuild := &ArchiveBuild{id: id, archive: a, dir: dir}

	return &ArtifactsBuild{backupArchiveBuild: backupArchiveBuild, id: id, archive: a, dir: dir, sshPublicKey: publicKey}, nil
}

// Download artifacts from the build id `buildID` and write them to `dst`.
// If `srcs` contains only one source, it will copy the file or directory
// directly to `dst`. Otherwise, `dst` should be the directory under which to
// download the artifacts.
func (a *Archive) download(ctx context.Context, buildID string, fromRoot bool, dst string, srcs []string) error {
	var src string
	var srcsFile string
	if len(srcs) > 1 {
		var filesToDownload []string
		for _, src := range srcs {
			path := filepath.Join(dst, src)

			if _, err := os.Stat(path); err != nil {
				filesToDownload = append(filesToDownload, src)
			}
		}

		if len(filesToDownload) == 0 {
			// Skip downloading if the files are already present in the build dir.
			return nil
		}

		tmpFile, err := ioutil.TempFile(dst, "srcs-file")
		if err != nil {
			return err
		}
		tmpFile.Close()
		srcsFile = tmpFile.Name()
		defer func() {
			os.Remove(srcsFile)
		}()
		if err := ioutil.WriteFile(srcsFile, []byte(strings.Join(filesToDownload, "\n")), 0755); err != nil {
			return fmt.Errorf("failed to write srcs-file: %w", err)
		}
	} else {
		if _, err := os.Stat(dst); err == nil {
			// Skip downloading if the file is already present in the build dir.
			return nil
		}
		src = srcs[0]
	}

	logger.Infof(ctx, "downloading to %s", dst)

	// We don't want to leak files if we are interrupted during a download.
	// So we'll remove all destination files in the case of an error.
	args := []string{
		"cp",
		"-build", buildID,
		"-src", src,
		"-dst", dst,
	}

	if fromRoot {
		args = append(args, "-root")
	}
	if srcsFile != "" {
		args = append(args, "-srcs-file", srcsFile)
	}

	_, stderr, err := util.RunCommand(ctx, a.artifactsPath, args...)
	if err != nil {
		defer os.RemoveAll(dst)
		if len(stderr) != 0 {
			return fmt.Errorf("artifacts failed: %w: %s", err, string(stderr))
		}
		return fmt.Errorf("artifacts failed: %w", err)
	}

	return nil
}

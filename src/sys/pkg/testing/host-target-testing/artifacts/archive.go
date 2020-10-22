// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package artifacts

import (
	"context"
	"fmt"
	"os"
	"path/filepath"
	"time"

	"golang.org/x/crypto/ssh"

	"go.fuchsia.dev/fuchsia/src/sys/pkg/testing/host-target-testing/util"
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

// GetBuilderByName looks up a build artifact by the given name.
func (a *Archive) GetBuilder(name string) *Builder {
	return &Builder{archive: a, name: name}
}

// GetBuildByID looks up a build artifact by the given id.
func (a *Archive) GetBuildByID(
	ctx context.Context,
	id string,
	dir string,
	publicKey ssh.PublicKey,
) (*ArchiveBuild, error) {
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

	return &ArchiveBuild{id: id, archive: a, dir: dir, sshPublicKey: publicKey}, nil
}

// Download an artifact from the build id `buildID` named `src` and write it
// into a directory `dst`.
func (a *Archive) download(ctx context.Context, dir string, buildID string, src string) (string, error) {
	basename := filepath.Base(src)
	buildDir := filepath.Join(dir, buildID)
	path := filepath.Join(buildDir, basename)

	// Skip downloading if the file is already present in the build dir.
	if _, err := os.Stat(path); err == nil {
		return path, nil
	}

	logger.Infof(ctx, "downloading %s to %s", src, path)

	if err := os.MkdirAll(buildDir, 0755); err != nil {
		return "", err
	}

	// We don't want to leak files if we are interrupted during a download.
	// So we'll initally download into a temporary file, and only once it
	// succeeds do we rename it into the real destination.
	err := util.AtomicallyWriteFile(path, 0644, func(tmpfile *os.File) error {
		// TODO(fxb/62503): Temporary measure to work around use of old version of cloud storage
		// api in prebuilt. This should be removed when the artifacts prebuilt is updated to
		// do the retry on network issues itself.
		eb := retry.NewExponentialBackoff(100*time.Millisecond, 10*time.Second, 2)
		// ~12 seconds to hit ceiling, plus 2.5 minutes of slack, given the above EB.
		retryCap := uint64(22)
		return retry.Retry(ctx, retry.WithMaxAttempts(eb, retryCap), func() error {
			args := []string{
				"cp",
				"-build", buildID,
				"-src", src,
				"-dst", tmpfile.Name(),
			}

			_, stderr, err := util.RunCommand(ctx, a.artifactsPath, args...)
			if err != nil {
				if len(stderr) != 0 {
					return fmt.Errorf("artifacts failed: %w: %s", err, string(stderr))
				}
				return fmt.Errorf("artifacts failed: %w", err)
			}
			return nil
		}, nil)
	})
	if err != nil {
		return "", err
	}

	return path, nil
}

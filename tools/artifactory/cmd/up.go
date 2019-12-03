// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// found in the LICENSE file.

package main

import (
	"bytes"
	"context"
	"crypto/md5"
	"flag"
	"fmt"
	"io"
	"io/ioutil"
	"os"
	"path"
	"path/filepath"
	"sync"

	"go.fuchsia.dev/fuchsia/tools/artifactory/lib"
	"go.fuchsia.dev/fuchsia/tools/build/lib"
	"go.fuchsia.dev/fuchsia/tools/lib/logger"

	"cloud.google.com/go/storage"
	"github.com/google/subcommands"
)

const (
	// The size in bytes at which files will be read and written to GCS.
	chunkSize = 8 * 1024 * 1024

	// Relative path within the build directory to the repo produced by a build.
	repoSubpath = "amber-files"
	// Names of the repository metadata, key, and blob directories within a repo.
	metadataDirName = "repository"
	keyDirName      = "keys"
	blobDirName     = "blobs"
	imageDirName    = "images"
)

type upCommand struct {
	// GCS bucket to which build artifacts will be uploaded.
	gcsBucket string
	// UUID under which to index artifacts.
	uuid string
	// The maximum number of concurrent uploading routines.
	j int
}

func (upCommand) Name() string { return "up" }

func (upCommand) Synopsis() string { return "upload artifacts from a build to Google Cloud Storage" }

func (upCommand) Usage() string {
	return `
artifactory up -bucket $GCS_BUCKET -uuid $UUID <build directory>

Uploads artifacts from a build to $GCS_BUCKET with the following structure:

├── $GCS_BUCKET
│   │   ├── blobs
│   │   │   └── <blob names>
│   │   ├── $UUID
│   │   │   ├── repository
│   │   │   │   └── <package repo metadata files>
│   │   │   ├── keys
│   │   │   │   └── <package repo keys>
│   │   │   ├── images
│   │   │   │   └── <images>

flags:

`
}

func (cmd *upCommand) SetFlags(f *flag.FlagSet) {
	f.StringVar(&cmd.gcsBucket, "bucket", "", "GCS bucket to which artifacts will be uploaded")
	f.StringVar(&cmd.uuid, "uuid", "", "UUID under which to index uploaded artifacts")
	f.IntVar(&cmd.j, "j", 1000, "maximum number of concurrent uploading processes")
}

func (cmd upCommand) Execute(ctx context.Context, f *flag.FlagSet, _ ...interface{}) subcommands.ExitStatus {
	args := f.Args()
	if len(args) != 1 {
		logger.Errorf(ctx, "exactly one positional argument expected: the build directory root")
		return subcommands.ExitFailure
	}

	if err := cmd.execute(ctx, args[0]); err != nil {
		logger.Errorf(ctx, "%v", err)
		return subcommands.ExitFailure
	}
	return subcommands.ExitSuccess
}

func (cmd upCommand) execute(ctx context.Context, buildDir string) error {
	if cmd.gcsBucket == "" {
		return fmt.Errorf("-bucket is required")
	} else if cmd.uuid == "" {
		return fmt.Errorf("-uuid is required")
	}

	m, err := build.NewModules(buildDir)
	if err != nil {
		return err
	}

	sink, err := newCloudSink(ctx, cmd.gcsBucket)
	if err != nil {
		return err
	}
	defer sink.client.Close()

	repo := path.Join(buildDir, repoSubpath)
	metadataDir := path.Join(repo, metadataDirName)
	keyDir := path.Join(repo, keyDirName)
	blobDir := path.Join(metadataDir, blobDirName)

	uploads := []struct {
		// Path on disk to a directory from which to upload.
		dir  artifactory.Upload
		opts uploadOptions
		// Specific files to upload.
		files []artifactory.Upload
	}{
		{
			dir: artifactory.Upload{
				Source:      blobDir,
				Destination: blobDirName,
			},
			opts: uploadOptions{
				// Note: there are O(10^3) blobs in a given clean build.
				j: cmd.j,
				// We want to dedup blobs across uploads.
				failOnCollision: false,
			},
		},
		{
			dir: artifactory.Upload{
				Source:      metadataDir,
				Destination: path.Join(cmd.uuid, metadataDirName),
			},
			opts: uploadOptions{
				// O(10^1) metadata files.
				j:               1,
				failOnCollision: true,
			},
		},
		{
			dir: artifactory.Upload{
				Source:      keyDir,
				Destination: path.Join(cmd.uuid, keyDirName),
			},
			opts: uploadOptions{
				// O(10^0) keys.
				j:               1,
				failOnCollision: true,
			},
		},
		{
			opts: uploadOptions{
				j:               1,
				failOnCollision: true,
			},
			files: artifactory.ImageUploads(m, path.Join(cmd.uuid, imageDirName)),
		},
	}

	for _, upload := range uploads {
		if upload.dir.Source != "" {
			if err = uploadDir(ctx, upload.dir, sink, upload.opts); err != nil {
				return err
			}
		}
		if err = uploadFiles(ctx, upload.files, sink, upload.opts); err != nil {
			return err
		}
	}
	return nil
}

// UploadOptions provides options to parametrize the upload behavior.
type uploadOptions struct {
	// Concurrency factor: number of separate uploading routines.
	j int

	// FailOnCollision indicates that an upload should fail if the object's
	// destination already exists.
	failOnCollision bool
}

// DataSink is an abstract data sink, providing a mockable interface to
// cloudSink, the GCS-backed implementation below.
type dataSink interface {

	// ObjectExistsAt takes a name and a checksum, and returns whether an object
	// of that name exists within the sink. If it does and has a checksum
	// different than the provided, a checksumError will be returned.
	objectExistsAt(context.Context, string, []byte) (bool, error)

	// Write writes the content of a file to a sink object at the given name.
	// If an object at that name does not exists, it will be created; else it
	// will be overwritten. If the written object has a checksum differing from
	// the provided checksum, then an error will be returned (not necessarily of
	// type checksumError, as this might derive from an opaque server-side error).
	write(context.Context, string, string, []byte) error
}

// CloudSink is a GCS-backed data sink.
type cloudSink struct {
	client *storage.Client
	bucket *storage.BucketHandle
}

func newCloudSink(ctx context.Context, bucket string) (*cloudSink, error) {
	client, err := storage.NewClient(ctx)
	if err != nil {
		return nil, err
	}
	return &cloudSink{
		client: client,
		bucket: client.Bucket(bucket),
	}, nil
}

func (s cloudSink) objectExistsAt(ctx context.Context, name string, expectedChecksum []byte) (bool, error) {
	obj := s.bucket.Object(name)
	attrs, err := obj.Attrs(ctx)
	if err == storage.ErrObjectNotExist {
		return false, nil
	} else if err != nil {
		return false, fmt.Errorf("object %q: possibly exists remotely, but is in an unknown state: %v", name, err)
	}
	if bytes.Compare(attrs.MD5, expectedChecksum) != 0 {
		return true, checksumError{
			name:     name,
			expected: expectedChecksum,
			actual:   attrs.MD5,
		}
	}
	return true, nil
}

func (s cloudSink) write(ctx context.Context, name string, path string, expectedChecksum []byte) error {
	w := s.bucket.Object(name).If(storage.Conditions{DoesNotExist: true}).NewWriter(ctx)
	w.ChunkSize = chunkSize
	w.MD5 = expectedChecksum

	fd, err := os.Open(path)
	if err != nil {
		return err
	}
	defer fd.Close()

	return artifactory.Copy(ctx, name, fd, w, chunkSize)
}

type checksumError struct {
	name     string
	expected []byte
	actual   []byte
}

func (err checksumError) Error() string {
	return fmt.Sprintf(
		"object %q: checksum mismatch: expected %v; actual %v",
		err.name, err.expected, err.actual,
	)
}

// dirToFiles returns a list of the top-level files in the dir.
func dirToFiles(dir artifactory.Upload) ([]artifactory.Upload, error) {
	var files []artifactory.Upload
	entries, err := ioutil.ReadDir(dir.Source)
	if err != nil {
		return nil, err
	}
	for _, fi := range entries {
		if fi.IsDir() {
			continue
		}
		files = append(files, artifactory.Upload{
			Source:      filepath.Join(dir.Source, fi.Name()),
			Destination: filepath.Join(dir.Destination, fi.Name()),
		})
	}
	return files, nil
}

func uploadDir(ctx context.Context, dir artifactory.Upload, dest dataSink, opts uploadOptions) error {
	if opts.j <= 0 {
		return fmt.Errorf("Concurrency factor j must be a positive number")
	}

	if _, err := os.Stat(dir.Source); err != nil {
		// The associated artifacts might not actually have been created, which is valid.
		if os.IsNotExist(err) {
			logger.Debugf(ctx, "%s does not exist; skipping upload", dir.Source)
			return nil
		}
		return err
	}
	files, err := dirToFiles(dir)
	if err != nil {
		return err
	}
	return uploadFiles(ctx, files, dest, opts)
}

func uploadFiles(ctx context.Context, files []artifactory.Upload, dest dataSink, opts uploadOptions) error {
	if opts.j <= 0 {
		return fmt.Errorf("Concurrency factor j must be a positive number")
	}

	uploads := make(chan artifactory.Upload, opts.j)
	errs := make(chan error, opts.j)

	queueUploads := func() {
		defer close(uploads)
		for _, f := range files {
			if _, err := os.Stat(f.Source); err != nil {
				// The associated artifacts might not actually have been created, which is valid.
				if os.IsNotExist(err) {
					logger.Debugf(ctx, "%s does not exist; skipping upload", f.Source)
					continue
				}
				errs <- err
				return
			}
			uploads <- f
		}
	}

	var wg sync.WaitGroup
	wg.Add(opts.j)
	upload := func() {
		defer wg.Done()
		for upload := range uploads {
			checksum, err := md5Checksum(upload.Source)
			if err != nil {
				errs <- err
				return
			}

			exists, err := dest.objectExistsAt(ctx, upload.Destination, checksum)
			if err != nil {
				errs <- err
				return
			}
			if exists {
				logger.Debugf(ctx, "object %q: already exists remotely", upload.Destination)
				if opts.failOnCollision {
					errs <- fmt.Errorf("object %q: collided", upload.Destination)
					return
				}
				continue
			}

			logger.Debugf(ctx, "object %q: attempting creation", upload.Destination)
			if err := dest.write(ctx, upload.Destination, upload.Source, checksum); err != nil {
				errs <- err
				return
			}
		}
	}

	go queueUploads()
	for i := 0; i < opts.j; i++ {
		go upload()
	}
	wg.Wait()
	close(errs)
	return <-errs
}

// Determines the checksum without reading all of the contents into memory.
func md5Checksum(file string) ([]byte, error) {
	fd, err := os.Open(file)
	if err != nil {
		return nil, err
	}
	defer fd.Close()
	h := md5.New()
	if _, err := io.Copy(h, fd); err != nil {
		return nil, err
	}
	checksum := h.Sum(nil)
	return checksum[:], nil
}

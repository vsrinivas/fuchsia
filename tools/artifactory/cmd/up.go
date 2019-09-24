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
	"net/url"
	"os"
	"path"
	"path/filepath"
	"strings"
	"sync"

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
)

type upCommand struct {
	// GCS root directory at which build artifacts will be uploaded.
	gcsRoot string
	// UUID under which to index artifacts.
	uuid string
	// The maximum number of concurrent uploading routines.
	j int
}

func (upCommand) Name() string { return "up" }

func (upCommand) Synopsis() string { return "upload artifacts from a build to Google Cloud Storage" }

func (upCommand) Usage() string {
	return `
artifactory up -root $GCS_ROOT -uuid $UUID <build directory>

Uploads artifacts from a build to $GCS_URL with the following structure:

├── $GCS_ROOT
│   │   ├── blobs
│   │   │   └── <blob names>
│   │   ├── $UUID
│   │   │   ├── repository
│   │   │   │   └── <package repo metadata files>
│   │   │   ├── keys
│   │   │   │   └── <package repo keys>

TODO(joshuaseaton): upload images to $GCS_PATH/$UUID/images/.

flags:

`
}

func (cmd *upCommand) SetFlags(f *flag.FlagSet) {
	f.StringVar(&cmd.gcsRoot, "root", "", "root GCS path at which to upload artifacts; must be of the form gs://<bucket>/<namespace>")
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
	if cmd.gcsRoot == "" {
		return fmt.Errorf("-root is required")
	} else if cmd.uuid == "" {
		return fmt.Errorf("-uuid is required")
	}

	url, err := url.Parse(cmd.gcsRoot)
	if err != nil {
		return err
	}
	bucket := url.Host
	namespace := strings.TrimLeft(url.Path, "/")
	if cmd.gcsRoot != fmt.Sprintf("gs://%s/%s", bucket, namespace) {
		return fmt.Errorf("invalid URL: -root %q not of the form gs://<bucket>/<namespace>", cmd.gcsRoot)
	}

	sink, err := newCloudSink(ctx, bucket, namespace)
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
		dir string
		// A namespace within the provided GCS bucket at which to upload.
		namespace string
		opts      uploadOptions
	}{
		{
			dir:       blobDir,
			namespace: blobDirName,
			opts: uploadOptions{
				// Note: there are O(10^3) blobs in a given clean build.
				j: cmd.j,
				// We want to dedup blobs across uploads.
				failOnCollision: false,
			},
		},
		{
			dir:       metadataDir,
			namespace: path.Join(cmd.uuid, metadataDirName),
			opts: uploadOptions{
				// O(10^1) metadata files.
				j:               1,
				failOnCollision: true,
			},
		},
		{
			dir:       keyDir,
			namespace: path.Join(cmd.uuid, keyDirName),
			opts: uploadOptions{
				// O(10^0) keys.
				j:               1,
				failOnCollision: true,
			},
		},
	}

	for _, upload := range uploads {
		dest := sink.subsinkAt(upload.namespace)
		if err = uploadFilesAt(ctx, upload.dir, dest, upload.opts); err != nil {
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

	// GetNamespace returns the namesapce of the sink under which object names are referenced.
	getNamespace() string

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
	client    *storage.Client
	bucket    *storage.BucketHandle
	namespace string
}

func newCloudSink(ctx context.Context, bucket, namespace string) (*cloudSink, error) {
	client, err := storage.NewClient(ctx)
	if err != nil {
		return nil, err
	}
	return &cloudSink{
		client:    client,
		bucket:    client.Bucket(bucket),
		namespace: namespace,
	}, nil
}

func (s *cloudSink) getNamespace() string {
	return s.namespace
}

func (s cloudSink) subsinkAt(subspace string) dataSink {
	return &cloudSink{
		client:    s.client,
		bucket:    s.bucket,
		namespace: path.Join(s.namespace, subspace),
	}
}

func (s cloudSink) objectExistsAt(ctx context.Context, name string, expectedChecksum []byte) (bool, error) {
	fullName := filepath.Join(s.getNamespace(), name)
	obj := s.bucket.Object(fullName)
	attrs, err := obj.Attrs(ctx)
	if err == storage.ErrObjectNotExist {
		return false, nil
	} else if err != nil {
		return false, fmt.Errorf("blob possibly exists remotely, but is in an uknown state: %v", err)
	}
	if bytes.Compare(attrs.MD5, expectedChecksum) != 0 {
		return true, checksumError{
			name:     name,
			expected: expectedChecksum,
			actual:   attrs.MD5,
		}
	}
	// Update the timestamp of the remote blob in this case, which is
	// accomplished by copying the object over itself. This should be a fast
	// metadata-only operation.
	_, err = obj.CopierFrom(obj).Run(ctx)
	return true, err
}

func (s cloudSink) write(ctx context.Context, name string, path string, expectedChecksum []byte) error {
	fullName := filepath.Join(s.getNamespace(), name)
	w := s.bucket.Object(fullName).NewWriter(ctx)
	w.ChunkSize = chunkSize
	w.MD5 = expectedChecksum

	fd, err := os.Open(path)
	if err != nil {
		return err
	}
	defer fd.Close()

	// Writes happen asynchronously, and so a nil may be returned while the write
	// goes on to fail. It is recommended in
	// https://godoc.org/cloud.google.com/go/storage#Writer.Write
	// to return the value of Close() to detect the success of the write.
	for {
		if _, err := io.CopyN(w, fd, chunkSize); err != nil {
			if err == io.EOF {
				break
			}
			w.Close()
			return err
		}
	}
	return w.Close()
}

type checksumError struct {
	name     string
	expected []byte
	actual   []byte
}

func (err checksumError) Error() string {
	return fmt.Sprintf(
		"blob %q checksum mismatch: expected %v; actual %v",
		err.name, err.expected, err.actual,
	)
}

func uploadFilesAt(ctx context.Context, src string, dest dataSink, opts uploadOptions) error {
	if opts.j <= 0 {
		return fmt.Errorf("Concurrency factor j must be a positive number")
	}
	names := make(chan string, opts.j)
	errs := make(chan error, opts.j)

	queueNames := func() {
		entries, err := ioutil.ReadDir(src)
		if err != nil {
			errs <- err
			return
		}
		for _, fi := range entries {
			if fi.IsDir() {
				continue
			}
			names <- fi.Name()
		}
		close(names)
	}

	var wg sync.WaitGroup
	wg.Add(opts.j)
	uploadNames := func() {
		defer wg.Done()
		for name := range names {
			fullName := filepath.Join(dest.getNamespace(), name)
			srcPath := filepath.Join(src, name)
			checksum, err := md5Checksum(srcPath)
			if err != nil {
				errs <- err
				return
			}

			exists, err := dest.objectExistsAt(ctx, name, checksum)
			if err != nil {
				errs <- err
				return
			}
			if exists {
				logger.Debugf(ctx, "object %q already exists remotely", fullName)
				if opts.failOnCollision {
					errs <- fmt.Errorf("object %q collided", fullName)
					return
				}
				continue
			}

			logger.Debugf(ctx, "writing to %s", fullName)
			if err := dest.write(ctx, name, srcPath, checksum); err != nil {
				errs <- err
				return
			}
		}
	}

	go queueNames()
	for i := 0; i < opts.j; i++ {
		go uploadNames()
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
	h := md5.New()
	if _, err := io.Copy(h, fd); err != nil {
		return nil, err
	}
	checksum := h.Sum(nil)
	return checksum[:], nil
}

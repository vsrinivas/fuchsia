// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// found in the LICENSE file.

package main

import (
	"bytes"
	"compress/gzip"
	"context"
	"crypto/md5"
	"flag"
	"fmt"
	"hash"
	"io"
	"io/ioutil"
	"os"
	"path"
	"path/filepath"
	"strings"
	"sync"
	"time"

	"cloud.google.com/go/storage"
	"github.com/google/subcommands"
	artifactory "go.fuchsia.dev/fuchsia/tools/artifactory/lib"
	build "go.fuchsia.dev/fuchsia/tools/build/lib"
	"go.fuchsia.dev/fuchsia/tools/lib/logger"
)

const (
	// The size in bytes at which files will be read and written to GCS.
	chunkSize = 8 * 1024 * 1024

	// Relative path within the build directory to the repo produced by a build.
	repoSubpath = "amber-files"
	// Names of the repository metadata, key, blob, and target directories within a repo.
	metadataDirName = "repository"
	keyDirName      = "keys"
	blobDirName     = "blobs"
	targetDirName   = "targets"

	// Names of directories to be uploaded to in GCS.
	buildsDirName  = "builds"
	debugDirName   = "debug"
	imageDirName   = "images"
	packageDirName = "packages"

	// A record of all of the fuchsia debug symbols processed.
	// This is eventually consumed by crash reporting infrastructure.
	buildIDsTxt = "build-ids.txt"
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
│   │   ├── debug
│   │   │   └── <debug binaries>
│   │   ├── builds
│   │   │   ├── $UUID
│   │   │   │   ├── packages
│   │   │   │   │   ├── repository
│   │   │   │   │   │   ├── targets
│   │   │   │   │   │   │   └── <package repo target files>
│   │   │   │   │   │   └── <package repo metadata files>
│   │   │   │   │   └── keys
│   │   │   │   │       └── <package repo keys>
│   │   │   │   ├── images
│   │   │   │   │   └── <images>

flags:

`
}

func (cmd *upCommand) SetFlags(f *flag.FlagSet) {
	f.StringVar(&cmd.gcsBucket, "bucket", "", "GCS bucket to which artifacts will be uploaded")
	f.StringVar(&cmd.uuid, "uuid", "", "UUID under which to index uploaded artifacts")
	f.IntVar(&cmd.j, "j", 500, "maximum number of concurrent uploading processes")
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
	// TODO(fxb/47901): Remove this line once clients stop passing the builds/ prefix.
	cmd.uuid = strings.TrimPrefix(cmd.uuid, "builds/")

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
	targetDir := path.Join(metadataDir, targetDirName)
	buildsUUIDDir := path.Join(buildsDirName, cmd.uuid)

	dirs := []artifactory.Upload{
		{
			Source:      blobDir,
			Destination: blobDirName,
			Deduplicate: true,
		},
		{
			Source:      metadataDir,
			Destination: path.Join(buildsUUIDDir, packageDirName, metadataDirName),
			Deduplicate: false,
		},
		{
			Source:      keyDir,
			Destination: path.Join(buildsUUIDDir, packageDirName, keyDirName),
			Deduplicate: false,
		},
		{
			Source:      targetDir,
			Destination: path.Join(buildsUUIDDir, packageDirName, metadataDirName, targetDirName),
			Deduplicate: false,
			Recursive:   true,
		},
	}

	var files []artifactory.Upload

	images := artifactory.ImageUploads(m, path.Join(buildsUUIDDir, imageDirName))
	files = append(files, images...)

	debugBinaries, buildIDs, err := artifactory.DebugBinaryUploads(m, debugDirName)
	if err != nil {
		return err
	}
	files = append(files, debugBinaries...)
	buildIDManifest, err := createBuildIDManifest(buildIDs)
	if err != nil {
		return err
	}
	defer os.Remove(buildIDManifest)
	files = append(files, artifactory.Upload{
		Source:      buildIDManifest,
		Destination: path.Join(buildsUUIDDir, buildIDsTxt),
	})

	for _, dir := range dirs {
		contents, err := dirToFiles(ctx, dir)
		if err != nil {
			return err
		}
		files = append(files, contents...)
	}
	return uploadFiles(ctx, files, sink, cmd.j)
}

func createBuildIDManifest(buildIDs []string) (string, error) {
	manifest, err := ioutil.TempFile("", buildIDsTxt)
	if err != nil {
		return "", err
	}
	defer manifest.Close()
	_, err = io.WriteString(manifest, strings.Join(buildIDs, "\n"))
	return manifest.Name(), err
}

// DataSink is an abstract data sink, providing a mockable interface to
// cloudSink, the GCS-backed implementation below.
type dataSink interface {

	// ObjectExistsAt returns whether an object of that name exists within the sink.
	objectExistsAt(ctx context.Context, name string) (bool, error)

	// Write writes the content of a file to a sink object at the given name.
	// If an object at that name does not exists, it will be created; else it
	// will be overwritten.
	write(ctx context.Context, name, path string) error
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

func (s *cloudSink) objectExistsAt(ctx context.Context, name string) (bool, error) {
	a, err := s.bucket.Object(name).Attrs(ctx)
	if err == storage.ErrObjectNotExist {
		return false, nil
	} else if err != nil {
		return false, fmt.Errorf("object %q: possibly exists remotely, but is in an unknown state: %v", name, err)
	}
	// Check if MD5 is not set, mark this as a miss, then write() function will
	// handle the race.
	return len(a.MD5) != 0, nil
}

// hasher is a io.Writer that calculates the MD5.
type hasher struct {
	h hash.Hash
	w io.Writer
}

func (h *hasher) Write(p []byte) (int, error) {
	n, err := h.w.Write(p)
	_, _ = h.h.Write(p[:n])
	return n, err
}

func getCompressedObjectWriter(ctx context.Context, obj *storage.ObjectHandle) *storage.Writer {
	w := obj.If(storage.Conditions{DoesNotExist: true}).NewWriter(ctx)
	w.ChunkSize = chunkSize
	w.ContentType = "application/octet-stream"
	w.ContentEncoding = "gzip"
	return w
}

func (s *cloudSink) write(ctx context.Context, name, path string) error {
	obj := s.bucket.Object(name)
	w := getCompressedObjectWriter(ctx, obj)

	fd, err := os.Open(path)
	if err != nil {
		return err
	}

	// We compress on the fly, and calculate the MD5 on the compressed data.
	h := hasher{md5.New(), w}
	gzw := gzip.NewWriter(&h)

	_, err = io.Copy(gzw, fd)
	// Writes happen asynchronously, and so a nil may be returned while the write
	// goes on to fail. It is recommended in
	// https://godoc.org/cloud.google.com/go/storage#Writer.Write
	// to return the value of Close() to detect the success of the write.
	// Both the gzip compressor and the socket must be closed, one after then
	// other.
	// Keep the first error we got.
	if err2 := gzw.Close(); err == nil {
		err = err2
	}
	if err2 := w.Close(); err == nil {
		err = err2
	}
	// Time to close the file.
	fd.Close()
	if err = checkGCSErr(ctx, err, name); err != nil {
		return err
	}

	// Now confirm that the MD5 matches upstream, just in case. If the file was
	// uploaded by another client (a race condition), loop until the MD5 is set.
	// This guarantees that the file is properly uploaded before this function
	// quits.
	d := h.h.Sum(nil)
	t := time.Second
	const max = 30 * time.Second
	for {
		attrs, err := obj.Attrs(ctx)
		if err != nil {
			return fmt.Errorf("failed to confirm MD5 for %s due to: %w", path, err)
		}
		if len(attrs.MD5) == 0 {
			time.Sleep(t)
			if t += t / 2; t > max {
				t = max
			}
			logger.Debugf(ctx, "waiting for MD5 for %s", path)
			continue
		}
		if !bytes.Equal(attrs.MD5, d) {
			return fmt.Errorf("MD5 mismatch for %s", path)
		}
		logger.Infof(ctx, "Uploaded: %s", path)
		break
	}
	return nil
}

// checkGCSErr validates the error for a GCS upload.
//
// If the precondition of the object not existing is not met on write (i.e.,
// at the time of the write the object is there), then the server will
// respond with a 412. (See
// https://cloud.google.com/storage/docs/json_api/v1/status-codes and
// https://tools.ietf.org/html/rfc7232#section-4.2.)
// We do not report this as an error, however, as the associated object might
// have been created after having checked its non-existence - and we wish to
// be resilient in the event of such a race.
func checkGCSErr(ctx context.Context, err error, name string) error {
	if err == nil || err == io.EOF {
		return nil
	}
	if strings.Contains(err.Error(), "Error 412") {
		logger.Infof(ctx, "object %q: created after its non-existence check", name)
		return nil
	}
	return err
}

// dirToFiles returns a list of the top-level files in the dir if dir.Recursive
// is false, else it returns all files in the dir.
func dirToFiles(ctx context.Context, dir artifactory.Upload) ([]artifactory.Upload, error) {
	var files []artifactory.Upload
	var err error
	var paths []string
	if dir.Recursive {
		err = filepath.Walk(dir.Source, func(path string, info os.FileInfo, err error) error {
			if err != nil {
				return err
			}
			if !info.IsDir() {
				relPath, err := filepath.Rel(dir.Source, path)
				if err != nil {
					return err
				}
				paths = append(paths, relPath)
			}
			return nil
		})
	} else {
		var entries []os.FileInfo
		entries, err = ioutil.ReadDir(dir.Source)
		if err == nil {
			for _, fi := range entries {
				if fi.IsDir() {
					continue
				}
				paths = append(paths, fi.Name())
			}
		}
	}
	if os.IsNotExist(err) {
		logger.Debugf(ctx, "%s does not exist; skipping upload", dir.Source)
		return nil, nil
	} else if err != nil {
		return nil, err
	}
	for _, path := range paths {
		files = append(files, artifactory.Upload{
			Source:      filepath.Join(dir.Source, path),
			Destination: filepath.Join(dir.Destination, path),
			Deduplicate: dir.Deduplicate,
		})
	}
	return files, nil
}

func uploadFiles(ctx context.Context, files []artifactory.Upload, dest dataSink, j int) error {
	if j <= 0 {
		return fmt.Errorf("Concurrency factor j must be a positive number")
	}

	uploads := make(chan artifactory.Upload, j)
	errs := make(chan error, j)

	queueUploads := func() {
		defer close(uploads)
		for _, f := range files {
			if _, err := os.Stat(f.Source); err != nil {
				// The associated artifacts might not actually have been created, which is valid.
				if os.IsNotExist(err) {
					logger.Infof(ctx, "%s does not exist; skipping upload", f.Source)
					continue
				}
				errs <- err
				return
			}
			uploads <- f
		}
	}

	var wg sync.WaitGroup
	wg.Add(j)
	upload := func() {
		defer wg.Done()
		for upload := range uploads {
			exists, err := dest.objectExistsAt(ctx, upload.Destination)
			if err != nil {
				errs <- err
				return
			}
			if exists {
				logger.Debugf(ctx, "object %q: already exists remotely", upload.Destination)
				if !upload.Deduplicate {
					errs <- fmt.Errorf("object %q: collided", upload.Destination)
					return
				}
				continue
			}

			logger.Debugf(ctx, "object %q: attempting creation", upload.Destination)
			if err := dest.write(ctx, upload.Destination, upload.Source); err != nil {
				errs <- fmt.Errorf("%s: %v", upload.Destination, err)
				return
			}
			logger.Debugf(ctx, "object %q: created", upload.Destination)
		}
	}

	go queueUploads()
	for i := 0; i < j; i++ {
		go upload()
	}
	wg.Wait()
	close(errs)
	return <-errs
}

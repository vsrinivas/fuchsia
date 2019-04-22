// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Uploads binary debug symbols to Google Cloud Storage.
//
// Example Usage:
//
// $ upload_debug_symbols -j 20 -bucket bucket-name /path/to/.build-id

package main

import (
	"context"
	"errors"
	"flag"
	"fmt"
	"log"
	"os"
	"sync"

	"cloud.google.com/go/storage"
	"fuchsia.googlesource.com/tools/elflib"
	"go.chromium.org/luci/auth/client/authcli"
	"go.chromium.org/luci/hardcoded/chromeinfra"
)

const (
	usage = `upload_debug_symbols [flags] [paths..]

	Uploads binary debug symbols to Google Cloud Storage.
	`

	// The default number of files to upload at once. The storage API returns 4xx errors
	// when we spawn too many go routines at once, and we can't predict the number of
	// symbol files we'll have to upload. 100 is chosen as a sensible default. This can be
	// overriden on the command line.
	defaultConccurrentUploadCount = 100
)

// Command line flags.
var (
	// The GCS bucket to upload files to.
	gcsBucket string

	// The maximum number of files to upload at once.
	concurrentUploadCount int

	authFlags authcli.Flags
)

func init() {
	flag.Usage = func() {
		fmt.Fprint(os.Stderr, usage)
		flag.PrintDefaults()
		os.Exit(1)
	}
	authDefaults := chromeinfra.DefaultAuthOptions()
	authDefaults.Scopes = append(authDefaults.Scopes, storage.ScopeReadWrite)
	authFlags.Register(flag.CommandLine, authDefaults)
	flag.StringVar(&gcsBucket, "bucket", "", "GCS Bucket to upload to")
	flag.IntVar(&concurrentUploadCount, "j", defaultConccurrentUploadCount, "Number of concurrent threads to use to upload files")
}

func main() {
	flag.Parse()
	if flag.NArg() == 0 {
		log.Fatal("expected at least one path to a .build-id directory")
	}
	if gcsBucket == "" {
		log.Fatal("missing -bucket")
	}
	if err := execute(context.Background(), flag.Args()); err != nil {
		log.Fatal(err)
	}
}

func execute(ctx context.Context, paths []string) error {
	bfrs, err := collectDebugSymbolFiles(paths)
	if err != nil {
		return fmt.Errorf("failed to collect symbol files: %v", err)
	}
	jobs, err := queueJobs(bfrs)
	if err != nil {
		return fmt.Errorf("failed to queue jobs: %v", err)
	}
	opts, err := authFlags.Options()
	if err != nil {
		return fmt.Errorf("failed to fetch GCS bucket information: %v", err)
	}
	bkt, err := newGCSBucket(ctx, gcsBucket, opts)
	if err != nil {
		return err
	}
	if !upload(ctx, bkt, jobs) {
		return errors.New("completed with errors")
	}
	return nil
}

// Creates BinaryFileRefs for all debug symbol files in the directories named in dirs.
func collectDebugSymbolFiles(dirs []string) ([]elflib.BinaryFileRef, error) {
	var out []elflib.BinaryFileRef
	for _, dir := range dirs {
		refs, err := elflib.WalkBuildIDDir(dir)
		if err != nil {
			return nil, err
		}
		out = append(out, refs...)
	}
	return out, nil
}

// Returns a read-only channel of jobs to upload each file referenced in bfrs.
func queueJobs(bfrs []elflib.BinaryFileRef) (<-chan job, error) {
	jobs := make(chan job, len(bfrs))
	for _, bfr := range bfrs {
		jobs <- job{
			dstBucket: gcsBucket,
			bfr:       bfr,
		}
	}
	close(jobs)
	return jobs, nil
}

// Upload executes all of the jobs to upload files from the input channel. Returns true
// iff all uploads succeeded without error.
func upload(ctx context.Context, bkt *GCSBucket, jobs <-chan job) (succeeded bool) {
	errs := make(chan error, defaultConccurrentUploadCount)
	defer close(errs)

	// Spawn workers to execute the uploads.
	workerCount := concurrentUploadCount
	var wg sync.WaitGroup
	wg.Add(workerCount)
	for i := 0; i < workerCount; i++ {
		go worker(ctx, bkt, &wg, jobs, errs)
	}

	// Let the caller know whether any errors were emitted.
	succeeded = true
	go func() {
		for e := range errs {
			succeeded = false
			log.Printf("error: %v", e)
		}
	}()
	wg.Wait()
	return succeeded
}

// worker processes all jobs on the input channel, emitting any errors on errs.
func worker(ctx context.Context, bkt *GCSBucket, wg *sync.WaitGroup, jobs <-chan job, errs chan<- error) {
	defer wg.Done()
	for job := range jobs {
		log.Printf("executing %s", job.name())
		if err := job.execute(context.Background(), bkt); err != nil {
			errs <- fmt.Errorf("job %s failed: %v", job.name(), err)
		}
	}
}

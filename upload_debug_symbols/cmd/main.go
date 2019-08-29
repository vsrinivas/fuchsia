// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Uploads binary debug symbols to Google Cloud Storage.
//
// Example Usage:
//
// $ upload_debug_symbols -j 20 -bucket bucket-name -upload-record /path/to/record /path/to/.build-id

package main

import (
	"context"
	"errors"
	"flag"
	"fmt"
	"io"
	"log"
	"os"
	"strings"
	"sync"

	"go.fuchsia.dev/tools/elflib"
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

	// GCS path to record of uploaded files.
	uploadRecord string

	// The maximum number of files to upload at once.
	concurrentUploadCount int
)

func init() {
	flag.Usage = func() {
		fmt.Fprint(os.Stderr, usage)
		flag.PrintDefaults()
		os.Exit(1)
	}
	flag.StringVar(&gcsBucket, "bucket", "", "GCS bucket to upload symbols to")
	flag.StringVar(&uploadRecord, "upload-record", "", "Path to write record of uploaded symbols")
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
	bfrs = filterInvalidDebugSymbolFiles(bfrs)
	jobs, err := queueJobs(bfrs)
	if err != nil {
		return fmt.Errorf("failed to queue jobs: %v", err)
	}
	bkt, err := newGCSBucket(ctx, gcsBucket)
	if err != nil {
		return err
	}
	succeeded, uploadPaths := upload(ctx, bkt, jobs)
	if !succeeded {
		return errors.New("completed with errors")
	}
	if uploadRecord != "" {
		if err = writeUploadRecord(uploadRecord, uploadPaths); err != nil {
			return fmt.Errorf("failed to write record of uploaded symbols: %v", err)
		}
		log.Printf("wrote record of uploaded symbols to %s\n", uploadRecord)
	}
	return nil
}

// Returns filtered input of BinaryFileRefs, skipping files without .debug_info header or valid build ID.
func filterInvalidDebugSymbolFiles(bfrs []elflib.BinaryFileRef) []elflib.BinaryFileRef {
	var filteredBfrs []elflib.BinaryFileRef
	for _, bfr := range bfrs {
		hasDebugInfo, err := bfr.HasDebugInfo()
		if err != nil {
			log.Printf("WARNING: cannot read file %s: %v, skipping\n", bfr.Filepath, err)
		} else if !hasDebugInfo {
			log.Printf("WARNING: file %s missing .debug_info section, skipping\n", bfr.Filepath)
		} else if err := bfr.Verify(); err != nil {
			log.Printf("WARNING: validation failed for %s: %v, skipping\n", bfr.Filepath, err)
		} else {
			filteredBfrs = append(filteredBfrs, bfr)
		}
	}
	return filteredBfrs
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
		jobs <- newJob(bfr, gcsBucket)
	}
	close(jobs)
	return jobs, nil
}

// Upload executes all of the jobs to upload files from the input channel. Returns true
// iff all uploads succeeded without error, and a record of all uploads as a string.
func upload(ctx context.Context, bkt *GCSBucket, jobs <-chan job) (bool, string) {
	errs := make(chan error, concurrentUploadCount)
	defer close(errs)
	uploadPaths := make(chan string, concurrentUploadCount)
	defer close(uploadPaths)

	// Spawn workers to execute the uploads.
	workerCount := concurrentUploadCount
	var wg sync.WaitGroup
	wg.Add(workerCount)
	for i := 0; i < workerCount; i++ {
		go worker(ctx, bkt, &wg, jobs, errs, uploadPaths)
	}

	// Let the caller know whether any errors were emitted.
	succeeded := true
	go func() {
		for e := range errs {
			succeeded = false
			log.Printf("error: %v", e)
		}
	}()
	// Receive from uploadPaths channel to build upload record.
	var builder strings.Builder
	go func() {
		for uploadPath := range uploadPaths {
			fmt.Fprintf(&builder, "%s\n", uploadPath)
		}
	}()
	wg.Wait()
	return succeeded, builder.String()
}

// worker processes all jobs on the input channel, emitting any errors on errs.
func worker(ctx context.Context, bkt *GCSBucket, wg *sync.WaitGroup, jobs <-chan job, errs chan<- error, uploadPaths chan<- string) {
	defer wg.Done()
	for job := range jobs {
		log.Printf("executing %s", job.name)
		err := job.execute(context.Background(), bkt)
		if err != nil {
			errs <- fmt.Errorf("job %s failed: %v", job.name, err)
		} else {
			uploadPaths <- job.gcsPath
		}
	}
}

// Write upload paths to local file.
func writeUploadRecord(uploadRecord string, uploadPaths string) error {
	file, err := os.Create(uploadRecord)
	if err != nil {
		return err
	}
	defer file.Close()
	_, err = io.WriteString(file, uploadPaths)
	return err
}

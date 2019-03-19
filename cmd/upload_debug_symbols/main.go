// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This tool upload debug symbols in ids.txt to Cloud storage
//
// Example Usage:
// $ upload_debug_symbols -bucket=/bucket_name -idsFilePath=/path/to/ids.txt

package main

import (
	"context"
	"flag"
	"fmt"
	"io/ioutil"
	"log"
	"os"
	"strings"
	"sync"

	"cloud.google.com/go/storage"
	"fuchsia.googlesource.com/tools/elflib"
	"google.golang.org/api/iterator"
)

const usage = `upload_debug_symbols [flags] bucket idsFilePath

Upload debug symbol files listed in ids.txt to Cloud storage
`

const (
	// The maximum number of files to upload at once. The storage API returns 4xx errors
	// when we spawn too many go routines at once, and we can't predict the number of
	// symbol files we'll have to upload.
	maxConcurrentUploads = 100
)

// Command line flag values
var (
	gcsBucket   string
	idsFilePath string
)

func init() {
	flag.Usage = func() {
		fmt.Fprint(os.Stderr, usage)
		flag.PrintDefaults()
		os.Exit(0)
	}

	flag.StringVar(&gcsBucket, "bucket", "", "The bucket to upload")
	flag.StringVar(&idsFilePath, "idsFilePath", "", "The path to file ids.txt")
}

func main() {
	flag.Parse()

	if gcsBucket == "" {
		log.Fatal("Error: gcsBucket is not specified.")
	}
	if idsFilePath == "" {
		log.Fatal("Error: idsFilePath is not specified.")
	}

	if err := execute(context.Background()); err != nil {
		log.Fatal(err)
	}
}

func execute(ctx context.Context) error {
	client, err := storage.NewClient(ctx)
	if err != nil {
		return fmt.Errorf("failed to create client: %v", err)
	}

	bkt := client.Bucket(gcsBucket)
	if _, err = bkt.Attrs(ctx); err != nil {
		return fmt.Errorf("failed to fetch bucket attributes: %v", err)
	}

	objMap, err := getObjects(context.Background(), bkt)
	if err != nil {
		return fmt.Errorf("failed to fetch object list from GCS: %v", err)
	}

	myClient := gcsClient{bkt: bkt, objMap: objMap}

	if err = uploadSymbolFiles(ctx, &myClient, idsFilePath); err != nil {
		return err
	}

	return nil
}

func uploadSymbolFiles(ctx context.Context, client GCSClient, idsFilePath string) error {
	file, err := os.Open(idsFilePath)
	if err != nil {
		return fmt.Errorf("failed to open %s: %v", idsFilePath, err)
	}
	defer file.Close()
	binaries, err := elflib.ReadIDsFile(file)
	if err != nil {
		return fmt.Errorf("failed to read %s with elflib: %v", idsFilePath, err)
	}

	var wg sync.WaitGroup
	jobsc := make(chan elflib.BinaryFileRef, len(binaries))
	const workerCount = maxConcurrentUploads
	for i := 0; i < workerCount; i++ {
		wg.Add(1)
		go worker(ctx, client, &wg, jobsc)
	}

	// Emit jobs
	for i := range binaries {
		jobsc <- binaries[i]
	}
	close(jobsc)

	wg.Wait() // Wait for workers to finish.
	return nil
}

func worker(ctx context.Context, client GCSClient, wg *sync.WaitGroup, jobs <-chan elflib.BinaryFileRef) {
	defer wg.Done()
	for binaryFileRef := range jobs {
		log.Printf("uploading %s", binaryFileRef.Filepath)
		objectName := binaryFileRef.BuildID + ".debug"
		if client.exists(objectName) {
			log.Printf("skipping %q which already exists", objectName)
			continue
		}
		if err := client.uploadSingleFile(context.Background(), objectName, binaryFileRef.Filepath); err != nil {
			log.Println(err)
		}
	}
}

func (client *gcsClient) exists(name string) bool {
	return client.objMap[name]
}

func (client *gcsClient) uploadSingleFile(ctx context.Context, name, localPath string) error {
	content, err := ioutil.ReadFile(localPath)
	if err != nil {
		return fmt.Errorf("unable to read file in uploadSingleFile: %v", err)
	}
	// This writer only perform write when the precondition of DoesNotExist is true.
	wc := client.bkt.Object(name).If(storage.Conditions{DoesNotExist: true}).NewWriter(ctx)
	if _, err := wc.Write(content); err != nil {
		return fmt.Errorf("failed to write to buffer of GCS onject writer: %v", err)
	}
	// Close completes the write operation and flushes any buffered data.
	if err := wc.Close(); err != nil {
		// Error 412 means the precondition of DoesNotExist doesn't match.
		// It is the expected behavior since we don't want to upload duplicated files.
		if !strings.Contains(err.Error(), "Error 412") {
			return fmt.Errorf("failed in close: %v", err)
		}
	}
	return nil
}

// getObjects returns a set of all binaries that currently exist in Cloud Storage.
// TODO(IN-1050)
func getObjects(ctx context.Context, bkt *storage.BucketHandle) (map[string]bool, error) {
	existingObjects := make(map[string]bool)
	it := bkt.Objects(ctx, nil)
	for {
		objAttrs, err := it.Next()
		if err == iterator.Done {
			break
		}
		if err != nil {
			return nil, err
		}
		existingObjects[objAttrs.Name] = true
	}
	return existingObjects, nil
}

// GCSClient provide method to upload single file to gcs
type GCSClient interface {
	uploadSingleFile(ctx context.Context, name, localPath string) error
	exists(object string) bool
}

// gcsClient is the object that implement GCSClient
type gcsClient struct {
	objMap map[string]bool
	bkt    *storage.BucketHandle
}

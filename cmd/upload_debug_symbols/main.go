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

	"cloud.google.com/go/storage"
	"fuchsia.googlesource.com/tools/elflib"
)

const usage = `upload_debug_symbols [flags] bucket idsFilePath

Upload debug symbol files listed in ids.txt to Cloud storage
`

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

	// Check if the bucket exists
	if _, err = bkt.Attrs(ctx); err != nil {
		return err
	}

	myClient := gcsClient{bkt}
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

	for _, binaryFileRef := range binaries {
		err := client.uploadSingleFile(ctx, binaryFileRef.BuildID+".debug", binaryFileRef.Filepath)
		if err != nil {
			return err
		}
	}
	return nil
}

func (client *gcsClient) uploadSingleFile(ctx context.Context, url string, filePath string) error {
	content, err := ioutil.ReadFile(filePath)
	if err != nil {
		return fmt.Errorf("unable to read file in uploadSingleFile: %v", err)
	}
	// This writer only perform write when the precondition of DoesNotExist is true.
	wc := client.bkt.Object(url).If(storage.Conditions{DoesNotExist: true}).NewWriter(ctx)
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

// GCSClient provide method to upload single file to gcs
type GCSClient interface {
	uploadSingleFile(ctx context.Context, url string, filePath string) error
}

// gcsClient is the object that implement GCSClient
type gcsClient struct {
	bkt *storage.BucketHandle
}

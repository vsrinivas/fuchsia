// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"cloud.google.com/go/storage"
	"context"
	"encoding/json"
	"flag"
	"fmt"
	"github.com/google/subcommands"
	"go.fuchsia.dev/fuchsia/tools/build"
	"go.fuchsia.dev/fuchsia/tools/lib/logger"
	"io/ioutil"
	"path/filepath"
	"strings"
	"time"
)

type downloadCmd struct {
	gcsBucket string
	buildIDs  string
}

const (
	buildsDirName = "builds"
	imageDirName  = "images"
	imageJSONName = "images.json"
)

func (*downloadCmd) Name() string { return "download" }

func (*downloadCmd) Synopsis() string {
	return "Downloads and updates product manifests to contain the absolute URIs and stores them in the out directory."
}

func (*downloadCmd) Usage() string {
	return "bundle_fetcher download -bucket <GCS_BUCKET> -build_ids <build_ids>\n"
}

func (cmd *downloadCmd) SetFlags(f *flag.FlagSet) {
	f.StringVar(&cmd.gcsBucket, "bucket", "", "GCS bucket from which to read the files from.")
	f.StringVar(&cmd.buildIDs, "build_ids", "", "Comma separated list of build_ids.")
}

func (cmd *downloadCmd) parseFlags() error {
	if cmd.buildIDs == "" {
		return fmt.Errorf("-build_ids is required")
	}
	if cmd.gcsBucket == "" {
		return fmt.Errorf("-bucket is required")
	}
	return nil
}

func (cmd *downloadCmd) Execute(ctx context.Context, f *flag.FlagSet, _ ...interface{}) subcommands.ExitStatus {
	if err := cmd.execute(ctx); err != nil {
		logger.Errorf(ctx, "%v", err)
		return subcommands.ExitFailure
	}
	return subcommands.ExitSuccess
}

func (cmd *downloadCmd) execute(ctx context.Context) error {
	if err := cmd.parseFlags(); err != nil {
		return err
	}

	sink, err := newCloudSink(ctx, cmd.gcsBucket)
	if err != nil {
		return err
	}
	defer sink.close()

	buildIDsList := strings.Split(cmd.buildIDs, ",")
	for _, buildID := range buildIDsList {
		buildsNamespaceDir := filepath.Join(buildsDirName, buildID)
		imageDir := filepath.Join(buildsNamespaceDir, imageDirName)
		imagesJSONPath := filepath.Join(imageDir, imageJSONName)

		productBundlePath, err := getProductBundlePathFromImagesJSON(ctx, sink, imagesJSONPath)
		if err != nil {
			return fmt.Errorf("unable to get product bundle path from images.json for build_id '%v': %v", buildID, err)
		}
		productBundleAbsPath := filepath.Join(imageDir, productBundlePath)
		logger.Debugf(ctx, "%v contains the product bundle in abs path %v", buildID, productBundleAbsPath)

		productBundleData, err := getProductBundleData(ctx, sink, productBundleAbsPath)
		if err != nil {
			return fmt.Errorf("unable to read product bundle data for build_id '%v': %v", buildID, err)
		}
		data, err := json.MarshalIndent(&productBundleData, "", "  ")
		if err != nil {
			return fmt.Errorf("unable to json marshall product bundle for build_id '%v': %v", buildID, err)
		}
		logger.Infof(ctx, "Product bundle for build_id %v:\n%v\n", buildID, string(data))
	}
	return nil
}

func getProductBundleData(ctx context.Context, sink dataSink, productBundleJSONPath string) (ProductBundle, error) {
	var productBundle ProductBundle
	data, err := sink.readFromGCS(ctx, productBundleJSONPath)
	if err != nil {
		return productBundle, err
	}
	err = json.Unmarshal(data, &productBundle)
	return productBundle, err
}

// getProductBundlePathFromImagesJson reads the images.json file in GCS and determines
// the product_bundle path based on that.
func getProductBundlePathFromImagesJSON(ctx context.Context, sink dataSink, imagesJSONPath string) (string, error) {
	data, err := sink.readFromGCS(ctx, imagesJSONPath)
	if err != nil {
		return "", err
	}
	var m build.ImageManifest
	err = json.Unmarshal(data, &m)
	if err != nil {
		return "", err
	}
	for _, entry := range m {
		if entry.Name == "product_bundle" {
			return entry.Path, nil
		}
	}
	return "", fmt.Errorf("unable to find product bundle in image manifest: %v", imagesJSONPath)
}

// DataSink is an abstract data sink, providing a mockable interface to
// cloudSink, the GCS-backed implementation below.
type dataSink interface {
	readFromGCS(ctx context.Context, object string) ([]byte, error)
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

func (s *cloudSink) close() {
	s.client.Close()
}

// readFromGCS reads an object from GCS.
func (s *cloudSink) readFromGCS(ctx context.Context, object string) ([]byte, error) {
	logger.Debugf(ctx, "reading %v from GCS", object)
	ctx, cancel := context.WithTimeout(ctx, time.Second*50)
	defer cancel()
	rc, err := s.bucket.Object(object).NewReader(ctx)
	if err != nil {
		return nil, err
	}
	defer rc.Close()

	data, err := ioutil.ReadAll(rc)
	if err != nil {
		return nil, err
	}
	return data, nil
}

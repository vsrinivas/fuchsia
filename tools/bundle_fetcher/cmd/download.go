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
	"google.golang.org/api/iterator"
	"io/ioutil"
	"net/url"
	"path/filepath"
	"strings"
	"time"
)

type downloadCmd struct {
	gcsBucket string
	buildIDs  string
}

const (
	buildsDirName       = "builds"
	imageDirName        = "images"
	imageJSONName       = "images.json"
	fileFormatName      = "files"
	gcsBaseURI          = "gs://"
	gsUtilURIFormatName = "gsutil_uri"
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

		updatedProductBundleData, err := readAndUpdateProductBundle(ctx, sink, productBundleAbsPath)
		if err != nil {
			return fmt.Errorf("unable to read product bundle data for build_id '%v': %v", buildID, err)
		}
		data, err := json.MarshalIndent(&updatedProductBundleData, "", "  ")
		if err != nil {
			return fmt.Errorf("unable to json marshall product bundle for build_id '%v': %v", buildID, err)
		}
		logger.Infof(ctx, "Product bundle for build_id %v:\n%v\n", buildID, string(data))
	}
	return nil
}

// getGCSURIBasedOnFileURI gets the gcs_uri based on the product_bundle path.
func getGCSURIBasedOnFileURI(ctx context.Context, sink dataSink, fileURI, productBundleJSONPath, bucket string) (string, error) {
	u, err := url.ParseRequestURI(fileURI)
	if err != nil {
		return "", err
	}
	productBundleDirPath := filepath.Dir(productBundleJSONPath)
	baseURI := filepath.Join(productBundleDirPath, u.Path)

	// Check that the path actually exists in GCS.
	validPath, err := sink.doesPathExist(ctx, baseURI)
	if err != nil {
		return "", err
	}
	if !validPath {
		return "", fmt.Errorf("base_uri is invalid %v", baseURI)
	}
	return gcsBaseURI + filepath.Join(bucket, baseURI), nil
}

// readAndUpdateProductBundle reads the product bundle from GCS and returns the ProductBundle
// with updated images/packages paths that point to a GCS URI.
func readAndUpdateProductBundle(ctx context.Context, sink dataSink, productBundleJSONPath string) (ProductBundle, error) {
	productBundleData, err := getProductBundleData(ctx, sink, productBundleJSONPath)
	if err != nil {
		return productBundleData, err
	}

	data := productBundleData.Data

	var newImages []*Image
	var newPackages []*Package

	logger.Debugf(ctx, "updating images for product bundle %q", productBundleJSONPath)
	for _, image := range data.Images {
		if image.Format == fileFormatName {
			gcsURI, err := getGCSURIBasedOnFileURI(ctx, sink, image.BaseURI, productBundleJSONPath, sink.getBucketName())
			if err != nil {
				return ProductBundle{}, err
			}
			logger.Debugf(ctx, "gcs_uri is %v for image base_uri %v", gcsURI, image.BaseURI)
			newImages = append(newImages, &Image{
				Format:  gsUtilURIFormatName,
				BaseURI: gcsURI,
			})
		}
	}

	logger.Debugf(ctx, "updating packages for product bundle %q", productBundleJSONPath)
	for _, pkg := range data.Packages {
		if pkg.Format == fileFormatName {
			gcsURI, err := getGCSURIBasedOnFileURI(ctx, sink, pkg.RepoURI, productBundleJSONPath, sink.getBucketName())
			if err != nil {
				return ProductBundle{}, err
			}
			logger.Debugf(ctx, "gcs_uri is %v for package repo_uri %v", gcsURI, pkg.RepoURI)
			newPackages = append(newPackages, &Package{
				Format:  gsUtilURIFormatName,
				RepoURI: gcsURI,
				BlobURI: pkg.BlobURI,
			})
		}
	}

	productBundleData.Data.Images = newImages
	productBundleData.Data.Packages = newPackages

	return productBundleData, nil
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
	getBucketName() string
	doesPathExist(ctx context.Context, prefix string) (bool, error)
}

// CloudSink is a GCS-backed data sink.
type cloudSink struct {
	client     *storage.Client
	bucket     *storage.BucketHandle
	bucketName string
}

func newCloudSink(ctx context.Context, bucket string) (*cloudSink, error) {
	client, err := storage.NewClient(ctx)
	if err != nil {
		return nil, err
	}
	return &cloudSink{
		client:     client,
		bucket:     client.Bucket(bucket),
		bucketName: bucket,
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

func (s *cloudSink) getBucketName() string {
	return s.bucketName
}

// doesPathExist checks if a path exists in GCS.
func (s *cloudSink) doesPathExist(ctx context.Context, prefix string) (bool, error) {
	logger.Debugf(ctx, "checking if %v is a valid path in GCS", prefix)
	it := s.bucket.Objects(ctx, &storage.Query{
		Prefix:    prefix,
		Delimiter: "/",
	})
	_, err := it.Next()
	// If the first object in the iterator is the end of the iterator, the path
	// is invalid and doesn't exist in GCS.
	if err == iterator.Done {
		return false, nil
	}
	if err != nil {
		return false, err
	}
	return true, nil
}

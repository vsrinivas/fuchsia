// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"context"
	"encoding/json"
	"flag"
	"fmt"
	"io/ioutil"
	"net/url"
	"os"
	"path/filepath"
	"reflect"
	"strings"
	"time"

	"go.fuchsia.dev/fuchsia/build/sdk/meta"
	"go.fuchsia.dev/fuchsia/tools/artifactory"
	"go.fuchsia.dev/fuchsia/tools/build"
	"go.fuchsia.dev/fuchsia/tools/lib/logger"

	"cloud.google.com/go/storage"
	"github.com/google/subcommands"
	"go.uber.org/multierr"
	"google.golang.org/api/iterator"
)

type downloadCmd struct {
	gcsBucket                   string
	buildIDs                    string
	outDir                      string
	outputProductBundleFileName string
}

type productBundleContainerArtifacts struct {
	productBundlePath  string
	deviceMetadataPath string
}

const (
	buildsDirName           = "builds"
	imageDirName            = "images"
	imageJSONName           = "images.json"
	fileFormatName          = "files"
	gcsBaseURI              = "gs://"
	pbmContainerType        = "product_bundle_container"
	pbmContainerName        = "sdk_product_bundle_container"
	pbmEntryName            = "product_bundle"
	virtualDeviceEntryName  = "virtual_device"
	physicalDeviceEntryName = "physical_device"
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
	f.StringVar(&cmd.outDir, "out_dir", "", "Directory to write out_file_name to.")
	f.StringVar(&cmd.outputProductBundleFileName, "out_file_name", "product_bundles.json", "Name of the output file containing all the product bundles.")
}

func (cmd *downloadCmd) parseFlags() error {
	if cmd.buildIDs == "" {
		return fmt.Errorf("-build_ids is required")
	}

	if cmd.gcsBucket == "" {
		return fmt.Errorf("-bucket is required")
	}

	if cmd.outDir == "" {
		return fmt.Errorf("-out_dir is required")
	}
	info, err := os.Stat(cmd.outDir)
	if os.IsNotExist(err) {
		return fmt.Errorf("out directory path %s does not exist", cmd.outDir)
	}
	if err != nil {
		return err
	}
	if !info.IsDir() {
		return fmt.Errorf("out directory path %s is not a directory", cmd.outDir)
	}
	return nil
}

func (cmd *downloadCmd) Execute(ctx context.Context, f *flag.FlagSet, _ ...interface{}) subcommands.ExitStatus {
	if err := cmd.execute(ctx); err != nil {
		logger.Errorf(ctx, "%s", err)
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

	productBundleContainer := meta.ProductBundleContainer{
		SchemaID: meta.PBMContainerSchemaID,
		Data: meta.ProductBundleContainerData{
			Type: pbmContainerType,
			Name: pbmContainerName,
		},
	}

	// knownDeviceMetadata is used to de-duplicate device metadata.
	knownDeviceMetadata := make(map[string][]byte)

	buildIDsList := strings.Split(cmd.buildIDs, ",")
	for _, buildID := range buildIDsList {
		buildID = strings.TrimSpace(buildID)
		buildsNamespaceDir := filepath.Join(buildsDirName, buildID)
		imageDir := filepath.Join(buildsNamespaceDir, imageDirName)
		imagesJSONPath := filepath.Join(imageDir, imageJSONName)

		artifact, err := getProductBundleContainerArtifactsFromImagesJSON(ctx, sink, imagesJSONPath)
		if err != nil {
			return fmt.Errorf("unable to find artifacts from images.json for build_id %s: %w", buildID, err)
		}
		productBundleAbsPath := filepath.Join(imageDir, artifact.productBundlePath)
		logger.Debugf(ctx, "%s contains the product bundle in abs path %s", buildID, productBundleAbsPath)

		updatedProductBundleData, err := readAndUpdateProductBundleData(ctx, sink, productBundleAbsPath)
		if err != nil {
			return fmt.Errorf("unable to read product bundle data for build_id %s: %w", buildID, err)
		}
		entry, err := convertMetadataToRawMessage(updatedProductBundleData)
		if err != nil {
			return fmt.Errorf("unable to convert product bundle metadata to json.RawMessage %w", err)
		}
		productBundleContainer.Data.Entries = append(productBundleContainer.Data.Entries, entry)

		deviceMetadataAbsPath := filepath.Join(imageDir, artifact.deviceMetadataPath)
		deviceData, isNew, err := readDeviceMetadata(ctx, sink, deviceMetadataAbsPath, &knownDeviceMetadata)
		if err != nil {
			return fmt.Errorf("unable to read device metadata data for build_id %s: %w", buildID, err)
		}
		// Only append the device metadata if it is new.
		if isNew {
			entry, err := convertMetadataToRawMessage(deviceData)
			if err != nil {
				return fmt.Errorf("unable to convert device metadata to json.RawMessage %w", err)
			}
			productBundleContainer.Data.Entries = append(productBundleContainer.Data.Entries, entry)
		}
	}

	logger.Debugf(ctx, "validating output data to make sure it follows the appropriate schema")
	if err := meta.ValidateProductBundleContainer(productBundleContainer); err != nil {
		return err
	}

	outputFilePath := filepath.Join(cmd.outDir, cmd.outputProductBundleFileName)
	logger.Debugf(ctx, "writing final product bundle file to: %s", outputFilePath)
	f, err := os.Create(outputFilePath)
	if err != nil {
		return err
	}
	var errs error
	enc := json.NewEncoder(f)
	enc.SetIndent("", "  ")
	if err := enc.Encode(&productBundleContainer); err != nil {
		errs = multierr.Append(errs, err)
	}
	if err := f.Close(); err != nil {
		errs = multierr.Append(errs, err)
	}
	return errs
}

// readDeviceMetadata reads the device metadata from GCS and checks that
// the metadata hasn't been seen already.
func readDeviceMetadata(ctx context.Context, sink dataSink, deviceMetadataPath string, knownDeviceMetadata *map[string][]byte) (*meta.DeviceMetadataData, bool, error) {
	var device meta.DeviceMetadata
	data, err := sink.readFromGCS(ctx, deviceMetadataPath)

	if err != nil {
		return nil, false, err
	}

	if err := json.Unmarshal(data, &device); err != nil {
		return nil, false, err
	}
	name := device.Data.Name
	// Check if the name has been seen already. If it isn't in the knownDeviceMetadata, that means it
	// is in new and exit early.
	if _, ok := (*knownDeviceMetadata)[name]; !ok {
		(*knownDeviceMetadata)[name] = data
		return &device.Data, true, nil
	}

	// Device metadata should have the same content if they have identical names.
	if !reflect.DeepEqual(data, (*knownDeviceMetadata)[name]) {
		return nil, false, fmt.Errorf("device metadata's have the same name %s but different values", name)
	}

	return &device.Data, false, nil
}

// convertMetadataToRawMessage converts metadata to json.RawMessage.
func convertMetadataToRawMessage(metadata interface{}) (json.RawMessage, error) {
	content, err := json.MarshalIndent(metadata, "", "  ")
	if err != nil {
		return nil, err
	}
	return json.RawMessage(content), err
}

// getGCSURIBasedOnFileURI gets the gcs_uri based on the product_bundle path.
func getGCSURIBasedOnFileURI(ctx context.Context, sink dataSink, fileURI, productBundleJSONPath, bucket string) (string, error) {
	u, err := url.ParseRequestURI(fileURI)
	if err != nil {
		return "", err
	}
	baseURI := filepath.Join(productBundleJSONPath, u.Path)
	// Check that the path actually exists in GCS.
	validPath, err := sink.doesPathExist(ctx, baseURI)
	if err != nil {
		return "", err
	}
	if !validPath {
		return "", fmt.Errorf("base_uri is invalid %s", baseURI)
	}
	return gcsBaseURI + filepath.Join(bucket, baseURI), nil
}

// readAndUpdateProductBundleData reads the product bundle from GCS and returns the ProductBundle Data
// with updated images/packages paths that point to a GCS URI.
func readAndUpdateProductBundleData(ctx context.Context, sink dataSink, productBundleJSONPath string) (*artifactory.Data, error) {
	productBundleData, err := getProductBundleData(ctx, sink, productBundleJSONPath)
	if err != nil {
		return nil, err
	}

	data := productBundleData.Data

	var newImages []*artifactory.Image
	var newPackages []*artifactory.Package

	logger.Debugf(ctx, "updating images for product bundle %s", productBundleJSONPath)
	for _, image := range data.Images {
		if image.Format == fileFormatName {
			gcsURI, err := getGCSURIBasedOnFileURI(ctx, sink, image.BaseURI, productBundleJSONPath, sink.getBucketName())
			if err != nil {
				return nil, err
			}
			logger.Debugf(ctx, "gcs_uri is %s for image base_uri %s", gcsURI, image.BaseURI)
			newImages = append(newImages, &artifactory.Image{
				Format:  fileFormatName,
				BaseURI: gcsURI,
			})
		}
	}

	logger.Debugf(ctx, "updating packages for product bundle %s", productBundleJSONPath)
	for _, pkg := range data.Packages {
		if pkg.Format == fileFormatName {
			repoURI, err := getGCSURIBasedOnFileURI(ctx, sink, pkg.RepoURI, productBundleJSONPath, sink.getBucketName())
			if err != nil {
				return nil, err
			}
			logger.Debugf(ctx, "gcs_uri is %s for package repo_uri %s", repoURI, pkg.RepoURI)

			blobURI, err := getGCSURIBasedOnFileURI(ctx, sink, pkg.BlobURI, productBundleJSONPath, sink.getBucketName())
			if err != nil {
				return nil, err
			}

			logger.Debugf(ctx, "gcs_uri is %s for package blob_uri %s", blobURI, pkg.BlobURI)
			newPackages = append(newPackages, &artifactory.Package{
				Format:  fileFormatName,
				RepoURI: repoURI,
				BlobURI: blobURI,
			})
		}
	}

	productBundleData.Data.Images = newImages
	productBundleData.Data.Packages = newPackages

	return &productBundleData.Data, nil
}

func getProductBundleData(ctx context.Context, sink dataSink, productBundleJSONPath string) (*artifactory.ProductBundle, error) {
	productBundle := &artifactory.ProductBundle{}
	data, err := sink.readFromGCS(ctx, productBundleJSONPath)
	if err != nil {
		return nil, err
	}
	err = json.Unmarshal(data, &productBundle)
	return productBundle, err
}

// getProductBundleContainerArtifactsFromImagesJSON reads the images.json file in GCS and determines
// the paths for product_bundle and either physical or virtual device metadata.
func getProductBundleContainerArtifactsFromImagesJSON(ctx context.Context, sink dataSink, imagesJSONPath string) (*productBundleContainerArtifacts, error) {
	artifact := &productBundleContainerArtifacts{}
	data, err := sink.readFromGCS(ctx, imagesJSONPath)
	if err != nil {
		return nil, err
	}
	var m build.ImageManifest
	err = json.Unmarshal(data, &m)
	if err != nil {
		return nil, err
	}
	for _, entry := range m {
		if entry.Name == pbmEntryName {
			artifact.productBundlePath = entry.Path
		} else if entry.Name == virtualDeviceEntryName || entry.Name == physicalDeviceEntryName {
			if artifact.deviceMetadataPath != "" {
				return nil, fmt.Errorf("found both a physical and virtual device metadata in the following paths: %q, %q."+
					" Should only have one.", artifact.deviceMetadataPath, entry.Path)
			}
			artifact.deviceMetadataPath = entry.Path
		}
	}
	// Ensure that a product bundle path exists.
	if artifact.productBundlePath == "" {
		return nil, fmt.Errorf("unable to find product bundle in image manifest: %s", imagesJSONPath)
	}
	// Ensure that either a physical or virtual device metadata path exists.
	if artifact.deviceMetadataPath == "" {
		return nil, fmt.Errorf("unable to find a physical or virtual device metadata in image manifest: %s", imagesJSONPath)
	}
	return artifact, nil
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
	logger.Debugf(ctx, "reading %s from GCS", object)
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
	logger.Debugf(ctx, "checking if %s is a valid path in GCS", prefix)
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

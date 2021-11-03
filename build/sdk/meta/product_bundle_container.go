// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package meta

import (
	"embed"
	"errors"
	"regexp"

	"go.fuchsia.dev/fuchsia/tools/artifactory"

	"github.com/xeipuuv/gojsonschema"
	"go.uber.org/multierr"
)

const (
	pbmContainerFileName = "product_bundle_container-76a5c104.json"
	PBMContainerSchemaID = "http://fuchsia.com/schemas/sdk/" + pbmContainerFileName
	// refRegexpString is used to capture objects in the schema with the
	// format: "$ref": "flash_manifest-835e8f26.json.
	refRegexpString = `"\$ref": "(.*)\.json`
	// refReplaceString is used to turn "$ref": "flash_manifest-835e8f26.json
	// into "$ref": "file://./flash_manifest-835e8f26.json.
	refReplaceString = `"ref": "file://./$1.json`
)

var (
	refRegexp = regexp.MustCompile(refRegexpString)
	//go:embed *.json
	jsonSchemas embed.FS
)

type ProductBundleContainer struct {
	SchemaID string `json:"schema_id"`
	Data     Data   `json:"data"`
}

type Data struct {
	Bundles []artifactory.ProductBundle `json:"bundles"`
	Type    string                      `json:"type"`
	Name    string                      `json:"name"`
}

// ValidateProductBundleContainer validates that the data is a valid
// based on product_bundle_container-76a5c104.json schema.
func ValidateProductBundleContainer(pbmContainer ProductBundleContainer) error {
	schemaLoader, err := loadProductBundleContainer()
	if err != nil {
		return err
	}
	outputLoader := gojsonschema.NewGoLoader(pbmContainer)
	result, err := schemaLoader.Validate(outputLoader)
	if err != nil {
		return err
	}
	if result.Valid() {
		return nil
	}
	var errs error
	for _, desc := range result.Errors() {
		errs = multierr.Append(errs, errors.New(desc.String()))
	}
	return errs
}

func readMetaAndUpdateRefToContainFileURIScheme(filePath string) (string, error) {
	data, err := jsonSchemas.ReadFile(filePath)
	if err != nil {
		return "", err
	}
	return refRegexp.ReplaceAllString(string(data), refReplaceString), nil
}

func loadProductBundleContainer() (*gojsonschema.Schema, error) {
	// Get a list of all files names that are stored in go:embed.
	files, err := jsonSchemas.ReadDir(".")
	if err != nil {
		return nil, err
	}
	loader := gojsonschema.NewSchemaLoader()
	for _, f := range files {
		// Skip the product_bundle_container schema as this is the main schema
		// that needs to be compiled.
		if f.Name() == pbmContainerFileName {
			continue
		}
		// Update the reference to confirm to the gojsonschema package requirement of URI
		// scheme requiring to have the prefix 'file://' and a full path to the file.
		metadata, err := readMetaAndUpdateRefToContainFileURIScheme(f.Name())
		if err != nil {
			return nil, err
		}
		fileLoader := gojsonschema.NewStringLoader(metadata)
		loader.AddSchema(f.Name(), fileLoader)
	}

	// Read the product_bundle_container as the main schema and compile it.
	metadata, err := readMetaAndUpdateRefToContainFileURIScheme(pbmContainerFileName)
	if err != nil {
		return nil, err
	}
	fileLoader := gojsonschema.NewStringLoader(metadata)
	return loader.Compile(fileLoader)
}

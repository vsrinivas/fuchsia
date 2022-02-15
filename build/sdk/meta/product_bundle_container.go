// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package meta

import (
	"embed"
	"encoding/json"
	"errors"
	"regexp"

	"github.com/xeipuuv/gojsonschema"
	"go.uber.org/multierr"
)

const (
	pbmContainerFileName = "product_bundle_container-32z5e391.json"
	// PBMContainerSchemaID represents the schema version for the current PBM container.
	PBMContainerSchemaID = "http://fuchsia.com/schemas/sdk/" + pbmContainerFileName
	// refRegexpString is used to capture objects in the schema with the
	// format: "$ref": "flash_manifest-835e8f26.json.
	refRegexpString = `ref": "(.*)\.json`
	// refReplaceString is used to turn "$ref": "flash_manifest-835e8f26.json
	// into "$ref": "http://fuchsia.com/schemas/sdk/flash_manifest-835e8f26.json.
	refReplaceString = `ref": "http://fuchsia.com/schemas/sdk/$1.json`
)

var (
	refRegexp = regexp.MustCompile(refRegexpString)
	//go:embed *.json
	jsonSchemas embed.FS
)

// ProductBundleContainer is a struct representing a PBM container.
type ProductBundleContainer struct {
	SchemaID string                     `json:"schema_id"`
	Data     ProductBundleContainerData `json:"data"`
}

// DeviceMetadata is a struct that contains device specifications.
type DeviceMetadata struct {
	Data     DeviceMetadataData `json:"data"`
	SchemaID string             `json:"schema_id"`
}

// Data contained in the device metadata.
type DeviceMetadataData struct {
	Description         json.RawMessage `json:"description"`
	Type                json.RawMessage `json:"type"`
	Name                string          `json:"name"`
	Hardware            json.RawMessage `json:"hardware"`
	Ports               json.RawMessage `json:"ports,omitempty"`
	StartUpArgsTemplate json.RawMessage `json:"start_up_args_template,omitempty"`
}

// Data contained in the PBM container.
type ProductBundleContainerData struct {
	Entries []json.RawMessage `json:"fms_entries"`
	Type    string            `json:"type"`
	Name    string            `json:"name"`
}

// ValidateProductBundleContainer validates that the data is a valid schema
// based on product_bundle_container-32z5e391.json schema.
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
		if err := loader.AddSchemas(fileLoader); err != nil {
			return nil, err
		}
	}

	// Read the product_bundle_container as the main schema and compile it.
	metadata, err := readMetaAndUpdateRefToContainFileURIScheme(pbmContainerFileName)
	if err != nil {
		return nil, err
	}
	fileLoader := gojsonschema.NewStringLoader(metadata)
	return loader.Compile(fileLoader)
}

// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package meta

import (
	"encoding/json"
	"errors"
	"testing"

	"github.com/google/go-cmp/cmp"
	"go.uber.org/multierr"
)

func compareMultierr(lhs, rhs error) bool {
	return (lhs == nil && rhs == nil) || (lhs != nil && rhs != nil && cmp.Equal(lhs.Error(), rhs.Error()))
}

func TestValidateProductBundleContainer(t *testing.T) {
	const validPBMContainerData = `{
		"schema_id": "http://fuchsia.com/schemas/sdk/product_bundle_container-76a5c104.json",
		"data": {
		  "bundles": [
			{
			  "data": {
				"device_refs": [
				  "x64"
				],
				"images": [
				  {
					"base_uri": "gs://fuchsia/images",
					"format": "files"
				  }
				],
				"type": "product_bundle",
				"name": "x64",
				"packages": [
				  {
					"format": "files",
					"blob_uri": "gs://fuchsia/packages",
					"repo_uri": "gs://fuchsia/packages"
				  }
				],
				"description": "",
				"metadata": [
				  [
					"is_debug",
					true
				  ]
				],
				"manifests": {
				  "flash": {
					"hw_revision": "x64",
					"products": [
					  {
						"name": "recovery",
						"bootloader_partitions": [],
						"oem_files": [],
						"partitions": [
						  {
							"name": "zircon_a",
							"path": "zedboot"
						  }
						]
					  },
					  {
						"name": "fuchsia",
						"bootloader_partitions": [],
						"oem_files": [],
						"partitions": [
						  {
							"name": "zircon_a",
							"path": "fuchsia"
						  }
						]
					  },
					  {
						"name": "bootstrap",
						"bootloader_partitions": [],
						"oem_files": [],
						"partitions": []
					  }
					]
				  }
				}
			  },
			  "schema_id": "http://fuchsia.com/schemas/sdk/product_bundle-6320eef1.json"
			}
		  ],
		  "type": "product_bundle_container",
		  "name": "sdk_product_bundle_container"
		}
	  }`
	var validPBMContainer ProductBundleContainer
	if err := json.Unmarshal([]byte(validPBMContainerData), &validPBMContainer); err != nil {
		t.Fatalf("json.Unmarshal(_, %T): %s", &validPBMContainer, err)
	}
	if err := ValidateProductBundleContainer(validPBMContainer); err != nil {
		t.Errorf("ValidateProductBundleContainer(%#v): %s", validPBMContainer, err)
	}

	const invalidPBMData = `{
		"schema_id": "http://fuchsia.com/schemas/sdk/product_bundle_container-76a5c104.json",
		"data": {
		  "bundles": [],
		  "type": "product_bundle_container",
		  "name": "sdk_product_bundle_container"
		}
	}`
	var invalidPBMContainer ProductBundleContainer
	if err := json.Unmarshal([]byte(invalidPBMData), &invalidPBMContainer); err != nil {
		t.Fatalf("json.Unmarshal(_, %T): %s", &invalidPBMContainer, err)
	}

	var want error
	want = multierr.Append(want, errors.New("data.bundles: Array must have at least 1 items"))
	want = multierr.Append(want, errors.New("(root): Must validate all the schemas (allOf)"))

	if diff := cmp.Diff(want, ValidateProductBundleContainer(invalidPBMContainer), cmp.Comparer(compareMultierr)); diff != "" {
		t.Errorf("ValidateProductBundleContainer() error mismatch (-want +got):\n%s", diff)
	}
}

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
		"schema_id":"http://fuchsia.com/schemas/sdk/product_bundle_container-32z5e391.json",
		"data":{
		  "fms_entries":[
			{
			  "device_refs":[
				"x64"
			  ],
			  "images":[
				{
				  "base_uri":"gs://fuchsia/images",
				  "format":"files"
				}
			  ],
			  "type":"product_bundle",
			  "name":"x64",
			  "packages":[
				{
				  "format":"files",
				  "blob_uri":"gs://fuchsia/packages",
				  "repo_uri":"gs://fuchsia/packages"
				}
			  ],
			  "description":"",
			  "metadata":[
				[
				  "is_debug",
				  true
				]
			  ],
			  "manifests":{
				"flash":{
				  "hw_revision":"x64",
				  "products":[
					{
					  "name":"recovery",
					  "bootloader_partitions":[

					  ],
					  "oem_files":[

					  ],
					  "partitions":[
						{
						  "name":"zircon_a",
						  "path":"zedboot"
						}
					  ]
					},
					{
					  "name":"fuchsia",
					  "bootloader_partitions":[

					  ],
					  "oem_files":[

					  ],
					  "partitions":[
						{
						  "name":"zircon_a",
						  "path":"fuchsia"
						}
					  ]
					},
					{
					  "name":"bootstrap",
					  "bootloader_partitions":[

					  ],
					  "oem_files":[

					  ],
					  "partitions":[

					  ]
					}
				  ]
				}
			  }
			},
			{
			  "type":"virtual_device",
			  "name":"qemu-x64",
			  "hardware":{
				"cpu":{
				  "arch":"x64"
				},
				"audio":{
				  "model":"hda"
				},
				"inputs":{
				  "pointing_device":"touch"
				},
				"window_size":{
				  "height":800,
				  "width":1280,
				  "units":"pixels"
				},
				"memory":{
				  "quantity":8192,
				  "units":"megabytes"
				},
				"storage":{
				  "quantity":2,
				  "units":"gigabytes"
				}
			  },
			  "ports": {
				"ssh": 22,
				"mdns": 5353,
				"debug": 2345
			  },
			  "start_up_args_template": "gen/build/images/emulator_flags.json.template"
			},
			{
			  "hardware":{
				"cpu":{
				  "arch":"x64"
				}
			  },
			  "name":"x64",
			  "type":"physical_device"
			}
		  ],
		  "type":"product_bundle_container",
		  "name":"sdk_product_bundle_container"
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
		"schema_id": "http://fuchsia.com/schemas/sdk/product_bundle_container-32z5e391.json",
		"data": {
		  "fms_entries": [],
		  "type": "product_bundle_container",
		  "name": "sdk_product_bundle_container"
		}
	}`
	var invalidPBMContainer ProductBundleContainer
	if err := json.Unmarshal([]byte(invalidPBMData), &invalidPBMContainer); err != nil {
		t.Fatalf("json.Unmarshal(_, %T): %s", &invalidPBMContainer, err)
	}

	var want error
	want = multierr.Append(want, errors.New("data.fms_entries: Array must have at least 1 items"))
	want = multierr.Append(want, errors.New("(root): Must validate all the schemas (allOf)"))

	if diff := cmp.Diff(want, ValidateProductBundleContainer(invalidPBMContainer), cmp.Comparer(compareMultierr)); diff != "" {
		t.Errorf("ValidateProductBundleContainer() error mismatch (-want +got):\n%s", diff)
	}
}

// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"io/ioutil"
	"os"
	"path/filepath"
	"reflect"
	"testing"

	"github.com/google/go-cmp/cmp"
)

// A simple in-memory implementation of dataSink.
type memSink struct {
	contents map[string][]byte
	err      error
	dir      string
}

func newMemSink(contents map[string][]byte, err error, dir string) *memSink {
	return &memSink{
		contents: contents,
		err:      err,
		dir:      dir,
	}
}

func (s *memSink) readFromGCS(ctx context.Context, object string) ([]byte, error) {
	if s.err != nil {
		return nil, s.err
	}
	if _, ok := s.contents[object]; !ok {
		return nil, fmt.Errorf("file not found")
	}
	return s.contents[object], nil
}

func (s *memSink) getBucketName() string {
	return s.dir
}

func (s *memSink) doesPathExist(ctx context.Context, prefix string) (bool, error) {
	if s.err != nil {
		return false, s.err
	}
	if _, ok := s.contents[prefix]; !ok {
		return false, nil
	}
	return true, nil
}

func TestParseFlags(t *testing.T) {
	dir, err := ioutil.TempDir("", "bundle_fetcher_dir")
	if err != nil {
		t.Fatalf("unable to create temp dir")
	}
	tmpfn := filepath.Join(dir, "tmpfile")
	if err := ioutil.WriteFile(tmpfn, []byte("hello world"), 0644); err != nil {
		t.Fatalf("unable to create temp file")
	}
	defer os.RemoveAll(dir)

	var tests = []struct {
		downloadCmd *downloadCmd
		expectedErr string
	}{
		{
			downloadCmd: &downloadCmd{
				buildIDs:  "123456",
				gcsBucket: "orange",
				outDir:    dir,
			},
		},
		{
			downloadCmd: &downloadCmd{
				gcsBucket: "orange",
				outDir:    dir,
			},
			expectedErr: "-build_ids is required",
		},
		{
			downloadCmd: &downloadCmd{
				buildIDs: "123456",
				outDir:   dir,
			},
			expectedErr: "-bucket is required",
		},
		{
			downloadCmd: &downloadCmd{
				buildIDs:  "123456",
				gcsBucket: "orange",
			},
			expectedErr: "-out_dir is required",
		},
		{
			downloadCmd: &downloadCmd{
				buildIDs:  "123456",
				gcsBucket: "orange",
				outDir:    tmpfn,
			},
			expectedErr: fmt.Sprintf("out directory path %v is not a directory", tmpfn),
		},
	}

	for _, test := range tests {
		if err := test.downloadCmd.parseFlags(); err != nil && err.Error() != test.expectedErr {
			t.Errorf("Got error: %s, want: %s", err.Error(), test.expectedErr)
		}
	}
}

func TestGetProductBundleContainerArtifactsFromImagesJSON(t *testing.T) {
	contents := map[string][]byte{
		"some/valid/physical/images.json": []byte(`[{
			"label": "//build/images:zedboot-script(//build/toolchain/fuchsia:arm64)",
			"name": "zedboot-script",
			"path": "pave-zedboot.sh",
			"type": "script"
		  },
		  {
			"label": "//build/images:product_metadata_json_generator(//build/toolchain/fuchsia:arm64)",
			"name": "product_bundle",
			"path": "gen/build/images/product_bundle.json",
			"type": "manifest"
		  },
		  {
			"label": "//build/images:product_metadata_json_generator(//build/toolchain/fuchsia:arm64)",
			"name": "physical_device",
			"path": "gen/build/images/physical_device.json",
			"type": "manifest"
		  }]`),
		"some/valid/virtual/images.json": []byte(`[{
			"label": "//build/images:zedboot-script(//build/toolchain/fuchsia:arm64)",
			"name": "zedboot-script",
			"path": "pave-zedboot.sh",
			"type": "script"
		  },
		  {
			"label": "//build/images:product_metadata_json_generator(//build/toolchain/fuchsia:arm64)",
			"name": "product_bundle",
			"path": "gen/build/images/product_bundle.json",
			"type": "manifest"
		  },
		  {
			"label": "//build/images:product_metadata_json_generator(//build/toolchain/fuchsia:arm64)",
			"name": "virtual_device",
			"path": "gen/build/images/virtual_device.json",
			"type": "manifest"
		  }]`),
		"some/missing/product/bundle/images.json": []byte(`[{
			"label": "//build/images:zedboot-script(//build/toolchain/fuchsia:arm64)",
			"name": "zedboot-script",
			"path": "pave-zedboot.sh",
			"type": "script"
		  }]`),
		"some/missing/physical/and/virtual/metadata/images.json": []byte(`[{
			"label": "//build/images:zedboot-script(//build/toolchain/fuchsia:arm64)",
			"name": "zedboot-script",
			"path": "pave-zedboot.sh",
			"type": "script"
		  },
		  {
			"label": "//build/images:product_metadata_json_generator(//build/toolchain/fuchsia:arm64)",
			"name": "product_bundle",
			"path": "gen/build/images/product_bundle.json",
			"type": "manifest"
		  }]`),
		"some/invalid/contains/both/virtual/and/physical/images.json": []byte(`[{
			"label": "//build/images:zedboot-script(//build/toolchain/fuchsia:arm64)",
			"name": "zedboot-script",
			"path": "pave-zedboot.sh",
			"type": "script"
		  },
		  {
			"label": "//build/images:product_metadata_json_generator(//build/toolchain/fuchsia:arm64)",
			"name": "product_bundle",
			"path": "gen/build/images/product_bundle.json",
			"type": "manifest"
		  },
		  {
			"label": "//build/images:product_metadata_json_generator(//build/toolchain/fuchsia:arm64)",
			"name": "physical_device",
			"path": "gen/build/images/physical_device.json",
			"type": "manifest"
		  },
		  {
			"label": "//build/images:product_metadata_json_generator(//build/toolchain/fuchsia:arm64)",
			"name": "virtual_device",
			"path": "gen/build/images/virtual_device.json",
			"type": "manifest"
		  }]`),
	}
	ctx := context.Background()
	var tests = []struct {
		name               string
		imageJSONPath      string
		dataSinkErr        error
		expectedOutput     *productBundleContainerArtifacts
		expectedErrMessage string
	}{
		{
			name:          "valid images.json with physical device",
			imageJSONPath: "some/valid/physical/images.json",
			expectedOutput: &productBundleContainerArtifacts{
				productBundlePath:  "gen/build/images/product_bundle.json",
				deviceMetadataPath: "gen/build/images/physical_device.json",
			},
		},
		{
			name:          "valid images.json with virtual device",
			imageJSONPath: "some/valid/virtual/images.json",
			expectedOutput: &productBundleContainerArtifacts{
				productBundlePath:  "gen/build/images/product_bundle.json",
				deviceMetadataPath: "gen/build/images/virtual_device.json",
			},
		},
		{
			name:               "product bundle is missing from images.json",
			imageJSONPath:      "some/missing/product/bundle/images.json",
			expectedErrMessage: "unable to find product bundle in image manifest: some/missing/product/bundle/images.json",
		},
		{
			name:               "images.json is missing from GCS",
			imageJSONPath:      "bucket/does/not/exist.json",
			dataSinkErr:        errors.New("storage: object doesn't exist"),
			expectedErrMessage: "storage: object doesn't exist",
		},
		{
			name:               "images.json is missing physical and virtual device metadata",
			imageJSONPath:      "some/missing/physical/and/virtual/metadata/images.json",
			expectedErrMessage: "unable to find a physical or virtual device metadata in image manifest: some/missing/physical/and/virtual/metadata/images.json",
		},
		{
			name:          "images.json contains both a physical and virtual device metadata",
			imageJSONPath: "some/invalid/contains/both/virtual/and/physical/images.json",
			expectedErrMessage: `found both a physical and virtual device metadata in the following paths: "gen/build/images/physical_device.json",` +
				` "gen/build/images/virtual_device.json". Should only have one.`,
		},
	}
	for _, test := range tests {
		t.Run(test.name, func(t *testing.T) {
			sink := newMemSink(contents, test.dataSinkErr, "")
			output, err := getProductBundleContainerArtifactsFromImagesJSON(ctx, sink, test.imageJSONPath)
			if !reflect.DeepEqual(output, test.expectedOutput) {
				t.Errorf("Got output: %v, want: %v", output, test.expectedOutput)
			}
			if err != nil && err.Error() != test.expectedErrMessage {
				t.Errorf("Got error: %s, want: %s", err.Error(), test.expectedErrMessage)
			}
			if err == nil && test.expectedErrMessage != "" {
				t.Errorf("Got no error, want: %s", test.expectedErrMessage)
			}
		})
	}
}

var (
	validVirtualDeviceMetadata = []byte(`{
		"data": {
		  "description": "A virtual x64 device",
		  "hardware": {
			  "cpu": {
				"arch": "x64"
			  },
			  "audio": {
				"model": "hda"
			  },
			  "inputs": {
				"pointing_device": "touch"
			  },
			  "window_size": {
				"height": 800,
				"width": 1280,
				"units": "pixels"
			  },
			  "memory": {
				"quantity": 8192,
				"units": "megabytes"
			  },
			  "storage": {
				"quantity": 2,
				"units": "gigabytes"
			  }
		  },
		  "ports": {
			"ssh": 22,
			"mdns": 5353,
			"debug": 2345
		  },
		  "start_up_args_template": "gen/build/images/emulator_flags.json.template",
		  "name": "qemu-x64",
		  "type": "virtual_device"
		},
		"schema_id": "http://fuchsia.com/schemas/sdk/virtual_device-93A41932.json"
	}`)
	validPhysicalDeviceMetadata = []byte(`{
		"data": {
		  "description": "A generic x64 device",
		  "hardware": {
			"cpu": {
			  "arch": "x64"
            }
		  },
		  "name": "x64",
		  "type": "physical_device"
		},
		"schema_id": "http://fuchsia.com/schemas/sdk/physical_device-0bd5d21f.json"
	}`)
	contentsDeviceMetadata = map[string][]byte{
		"builds/123456/images/gen/build/images/virtual/device/one.json":  validVirtualDeviceMetadata,
		"builds/123456/images/gen/build/images/physical/device/one.json": validPhysicalDeviceMetadata,
		"some/invalid/virtual/device.json": []byte(`{
		  "data": "I am a string instead of an object",
		  "schema_id": "http://fuchsia.com/schemas/sdk/virtual_device-93A41932.json"
	  	}`),
	}
)

func TestReadDeviceMetadata(t *testing.T) {
	ctx := context.Background()
	var tests = []struct {
		name                   string
		knownDeviceMetadata    *map[string][]byte
		dir                    string
		deviceMetadataPath     string
		expectedIsNew          bool
		expectedDeviceMetadata string
	}{
		{
			name:                "valid virtual device metadata that is new",
			dir:                 "fuchsia",
			deviceMetadataPath:  "builds/123456/images/gen/build/images/virtual/device/one.json",
			knownDeviceMetadata: &map[string][]byte{},
			expectedIsNew:       true,
			expectedDeviceMetadata: `{
  "description": "A virtual x64 device",
  "type": "virtual_device",
  "name": "qemu-x64",
  "hardware": {
    "cpu": {
      "arch": "x64"
    },
    "audio": {
      "model": "hda"
    },
    "inputs": {
      "pointing_device": "touch"
    },
    "window_size": {
      "height": 800,
      "width": 1280,
      "units": "pixels"
    },
    "memory": {
      "quantity": 8192,
      "units": "megabytes"
    },
    "storage": {
      "quantity": 2,
      "units": "gigabytes"
    }
  },
  "ports": {
    "ssh": 22,
    "mdns": 5353,
    "debug": 2345
  },
  "start_up_args_template": "gen/build/images/emulator_flags.json.template"
}`,
		},
		{
			name:               "valid physical device metadata that is new",
			dir:                "fuchsia",
			deviceMetadataPath: "builds/123456/images/gen/build/images/physical/device/one.json",
			knownDeviceMetadata: &map[string][]byte{
				"qemu-x64": validVirtualDeviceMetadata,
			},
			expectedIsNew: true,
			expectedDeviceMetadata: `{
  "description": "A generic x64 device",
  "type": "physical_device",
  "name": "x64",
  "hardware": {
    "cpu": {
      "arch": "x64"
    }
  }
}`,
		},
		{
			name: "valid virtual device metadata that already exists with identical data",
			dir:  "fuchsia",
			knownDeviceMetadata: &map[string][]byte{
				"qemu-x64": validVirtualDeviceMetadata,
			},
			deviceMetadataPath: "builds/123456/images/gen/build/images/virtual/device/one.json",
			expectedIsNew:      false,
			expectedDeviceMetadata: `{
  "description": "A virtual x64 device",
  "type": "virtual_device",
  "name": "qemu-x64",
  "hardware": {
    "cpu": {
      "arch": "x64"
    },
    "audio": {
      "model": "hda"
    },
    "inputs": {
      "pointing_device": "touch"
    },
    "window_size": {
      "height": 800,
      "width": 1280,
      "units": "pixels"
    },
    "memory": {
      "quantity": 8192,
      "units": "megabytes"
    },
    "storage": {
      "quantity": 2,
      "units": "gigabytes"
    }
  },
  "ports": {
    "ssh": 22,
    "mdns": 5353,
    "debug": 2345
  },
  "start_up_args_template": "gen/build/images/emulator_flags.json.template"
}`,
		},
	}

	for _, test := range tests {
		t.Run(test.name, func(t *testing.T) {
			sink := newMemSink(contentsDeviceMetadata, nil, test.dir)
			output, isNew, err := readDeviceMetadata(ctx, sink, test.deviceMetadataPath, test.knownDeviceMetadata)
			if err != nil {
				t.Errorf("Got error: %s, expected no error", err)
			}
			if isNew != test.expectedIsNew {
				t.Errorf("readDeviceMetadata() got: %t, want: %t", isNew, test.expectedIsNew)
			}
			marshalledOutput, err := json.MarshalIndent(&output, "", "  ")
			if err != nil {
				t.Fatalf("json.MarshalIndent(%#v): %s", &output, err)
			}
			if diff := cmp.Diff(test.expectedDeviceMetadata, string(marshalledOutput)); diff != "" {
				t.Errorf("unexpected device metadata (-want +got):\n%v", diff)
			}
		})
	}
}

func TestReadDeviceMetadataInvalid(t *testing.T) {
	ctx := context.Background()
	var tests = []struct {
		name                string
		deviceMetadataPath  string
		knownDeviceMetadata *map[string][]byte
		dir                 string
		dataSinkErr         error
		expectedErrMessage  string
	}{
		{
			name:               "device metadata does not exist in GCS",
			deviceMetadataPath: "device/does/not/exist.json",
			dataSinkErr:        errors.New("storage: object doesn't exist"),
			expectedErrMessage: "storage: object doesn't exist",
		},
		{
			name:               "device metadata contains incorrect json schema",
			deviceMetadataPath: "some/invalid/virtual/device.json",
			expectedErrMessage: "json: cannot unmarshal string into Go struct field DeviceMetadata.data of type meta.DeviceMetadataData",
		},
		{
			name: "device metadata has same name but different values in metadata",
			dir:  "fuchsia",
			knownDeviceMetadata: &map[string][]byte{
				"qemu-x64": []byte("{some-thing-invalid}"),
			},
			deviceMetadataPath: "builds/123456/images/gen/build/images/virtual/device/one.json",
			expectedErrMessage: "device metadata's have the same name qemu-x64 but different values",
		},
	}
	for _, test := range tests {
		t.Run(test.name, func(t *testing.T) {
			sink := newMemSink(contentsDeviceMetadata, test.dataSinkErr, test.dir)
			_, _, err := readDeviceMetadata(ctx, sink, test.deviceMetadataPath, test.knownDeviceMetadata)
			if err != nil && err.Error() != test.expectedErrMessage {
				t.Errorf("Got error: %s, want: %s", err.Error(), test.expectedErrMessage)
			}
			if err == nil && test.expectedErrMessage != "" {
				t.Errorf("Got no error, want: %s", test.expectedErrMessage)
			}
		})
	}
}

var contentsProductBundle = map[string][]byte{
	"builds/123456/images/gen/build/images/emulator.json": []byte(`{
		"data": {
		  "description": "some emulator device",
		  "metadata": [
			[
			  "is_debug",
			  false
			]
		  ],
		  "device_refs": [
			"qemu-x64"
		  ],
		  "images": [
			{
			  "base_uri": "file:/../../../..",
			  "format": "files"
			}
		  ],
		  "name": "terminal.qemu-x64",
		  "packages": [
			{
			  "format": "files",
			  "blob_uri": "file:/../../../../../../../blobs",
			  "repo_uri": "file:/../../../../../packages"
			}
		  ],
		  "type": "product_bundle",
		  "manifests": {
			"emu": {
			  "disk_images": [
				"obj/build/images/fuchsia/fuchsia/fvm.blob.sparse.blk"
			  ],
			  "initial_ramdisk": "fuchsia.zbi",
			  "kernel": "multiboot.bin"
			}
		   }
		},
		"schema_id": "http://fuchsia.com/schemas/sdk/product_bundle-6320eef1.json"
		}`),
	"builds/789123/images/gen/build/images/physical_device.json": []byte(`{
			"data": {
			  "description": "",
			  "device_refs": [
				"x64"
			  ],
			  "images": [
				{
				  "base_uri": "file:/../../../..",
				  "format": "files"
				}
			  ],
			  "manifests": {
				"flash": {
				  "hw_revision": "x64",
				  "products": [
					{
					  "name": "fuchsia",
					  "bootloader_partitions": [
						{
						  "name": "fuchsia-esp",
						  "path": "fuchsia.esp.blk"
						}
					  ],
					  "oem_files": [],
					  "partitions": [
						{
						  "name": "a",
						  "path": "zbi"
						},
						{
						  "name": "r",
						  "path": "vbmeta"
						}
					  ]
					}
				  ]
				}
			  },
			  "metadata": [
				[
				  "build_info_board",
				  "x64"
				]
			  ],
			  "name": "terminal.x64",
			  "packages": [
				{
				  "format": "files",
				  "blob_uri": "file:/../../../../../../../blobs",
				  "repo_uri": "file:/../../../../../packages"
				}
			  ],
			  "type": "product_bundle"
			},
			"schema_id": "http://fuchsia.com/schemas/sdk/product_bundle-6320eef1.json"
		  }`),
	"builds/invalid/path/images/gen/build/images/emulator.json": []byte(`{
			"data": {
			  "description": "some invalid emulator device",
			  "device_refs": [
				"qemu-x64"
			  ],
			  "images": [
				{
				  "base_uri": "file:/../../../..",
				  "format": "files"
				}
			  ],
			  "name": "terminal.qemu-x64",
			  "packages": [
				{
				  "format": "files",
				  "blob_uri": "file:/../../../../../../../blobs",
				  "repo_uri": "file:/../../../../../packages"
				}
			  ],
			  "type": "product_bundle",
			  "manifests": {
				"emu": {
				  "disk_images": [
					"obj/build/images/fuchsia/fuchsia/fvm.blob.sparse.blk"
				  ],
				  "initial_ramdisk": "fuchsia.zbi",
				  "kernel": "multiboot.bin"
				}
			   }
			},
			"schema_id": "http://fuchsia.com/schemas/sdk/product_bundle-6320eef1.json"
			}`),
	"some/invalid/product_bundle.json": []byte(`{
			  "data": "I am a string instead of an object",
			"schema_id": "http://fuchsia.com/schemas/sdk/product_bundle-6320eef1.json"
		  }`),
	"builds/123456/images":   []byte(""),
	"builds/123456/packages": []byte(""),
	"builds/789123/images":   []byte(""),
	"builds/789123/packages": []byte(""),
	"blobs":                  []byte(""),
}

func TestGetProductBundleData(t *testing.T) {
	ctx := context.Background()
	var tests = []struct {
		name                  string
		productBundlePath     string
		dir                   string
		expectedProductBundle string
	}{
		{
			name:              "valid product bundle for emulator",
			productBundlePath: "builds/123456/images/gen/build/images/emulator.json",
			expectedProductBundle: `{
  "device_refs": [
    "qemu-x64"
  ],
  "images": [
    {
      "base_uri": "gs://fuchsia/builds/123456/images",
      "format": "files"
    }
  ],
  "type": "product_bundle",
  "name": "terminal.qemu-x64",
  "packages": [
    {
      "format": "files",
      "blob_uri": "gs://fuchsia/blobs",
      "repo_uri": "gs://fuchsia/builds/123456/packages"
    }
  ],
  "description": "some emulator device",
  "metadata": [
    [
      "is_debug",
      false
    ]
  ],
  "manifests": {
    "emu": {
      "disk_images": [
        "obj/build/images/fuchsia/fuchsia/fvm.blob.sparse.blk"
      ],
      "initial_ramdisk": "fuchsia.zbi",
      "kernel": "multiboot.bin"
    }
  }
}`,
			dir: "fuchsia",
		},
		{
			name:              "valid product bundle for physical device",
			productBundlePath: "builds/789123/images/gen/build/images/physical_device.json",
			expectedProductBundle: `{
  "device_refs": [
    "x64"
  ],
  "images": [
    {
      "base_uri": "gs://fuchsia/builds/789123/images",
      "format": "files"
    }
  ],
  "type": "product_bundle",
  "name": "terminal.x64",
  "packages": [
    {
      "format": "files",
      "blob_uri": "gs://fuchsia/blobs",
      "repo_uri": "gs://fuchsia/builds/789123/packages"
    }
  ],
  "description": "",
  "metadata": [
    [
      "build_info_board",
      "x64"
    ]
  ],
  "manifests": {
    "flash": {
      "hw_revision": "x64",
      "products": [
        {
          "name": "fuchsia",
          "bootloader_partitions": [
            {
              "name": "fuchsia-esp",
              "path": "fuchsia.esp.blk"
            }
          ],
          "oem_files": [],
          "partitions": [
            {
              "name": "a",
              "path": "zbi"
            },
            {
              "name": "r",
              "path": "vbmeta"
            }
          ]
        }
      ]
    }
  }
}`,
			dir: "fuchsia",
		},
	}
	for _, test := range tests {
		t.Run(test.name, func(t *testing.T) {
			sink := newMemSink(contentsProductBundle, nil, test.dir)
			output, err := readAndUpdateProductBundleData(ctx, sink, test.productBundlePath)
			if err != nil {
				t.Errorf("Got error: %s, expected no error", err)
			}
			marshalledOutput, err := json.MarshalIndent(&output, "", "  ")
			if err != nil {
				t.Fatalf("json.MarshalIndent(%#v): %s", &output, err)
			}
			if diff := cmp.Diff(test.expectedProductBundle, string(marshalledOutput)); diff != "" {
				t.Errorf("unexpected image uploads (-want +got):\n%v", diff)
			}
		})
	}
}

func TestGetProductBundleDataInvalid(t *testing.T) {
	ctx := context.Background()
	var tests = []struct {
		name               string
		productBundlePath  string
		dir                string
		dataSinkErr        error
		expectedErrMessage string
	}{
		{
			name:               "product bundle does not exist in GCS",
			productBundlePath:  "product/bundle/does/not/exist.json",
			dataSinkErr:        errors.New("storage: object doesn't exist"),
			expectedErrMessage: "storage: object doesn't exist",
		},
		{
			name:               "product bundle contains incorrect json schema",
			productBundlePath:  "some/invalid/product_bundle.json",
			expectedErrMessage: "json: cannot unmarshal string into Go struct field ProductBundle.data of type artifactory.Data",
		},
		{
			name:               "gcs prefix doesn't exist",
			productBundlePath:  "builds/invalid/path/images/gen/build/images/emulator.json",
			expectedErrMessage: "base_uri is invalid builds/invalid/path/images",
		},
	}
	for _, test := range tests {
		t.Run(test.name, func(t *testing.T) {
			sink := newMemSink(contentsProductBundle, test.dataSinkErr, test.dir)
			_, err := readAndUpdateProductBundleData(ctx, sink, test.productBundlePath)
			if err != nil && err.Error() != test.expectedErrMessage {
				t.Errorf("Got error: %s, want: %s", err.Error(), test.expectedErrMessage)
			}
			if err == nil && test.expectedErrMessage != "" {
				t.Errorf("Got no error, want: %s", test.expectedErrMessage)
			}
		})
	}
}

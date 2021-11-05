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

func TestGetProductBundlePathFromImagesJSON(t *testing.T) {
	contents := map[string][]byte{
		"some/valid/images.json": []byte(`[{
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
		"some/missing/product/bundle/images.json": []byte(`[{
			"label": "//build/images:zedboot-script(//build/toolchain/fuchsia:arm64)",
			"name": "zedboot-script",
			"path": "pave-zedboot.sh",
			"type": "script"
		  }]`),
	}
	ctx := context.Background()
	var tests = []struct {
		name               string
		imageJSONPath      string
		dataSinkErr        error
		expectedOutput     string
		expectedErrMessage string
	}{
		{
			name:           "valid images.json",
			imageJSONPath:  "some/valid/images.json",
			expectedOutput: "gen/build/images/product_bundle.json",
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
	}
	for _, test := range tests {
		t.Run(test.name, func(t *testing.T) {
			sink := newMemSink(contents, test.dataSinkErr, "")
			output, err := getProductBundlePathFromImagesJSON(ctx, sink, test.imageJSONPath)
			if output != test.expectedOutput {
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

var contents = map[string][]byte{
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
  "data": {
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
  },
  "schema_id": "http://fuchsia.com/schemas/sdk/product_bundle-6320eef1.json"
}`,
			dir: "fuchsia",
		},
		{
			name:              "valid product bundle for physical device",
			productBundlePath: "builds/789123/images/gen/build/images/physical_device.json",
			expectedProductBundle: `{
  "data": {
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
  },
  "schema_id": "http://fuchsia.com/schemas/sdk/product_bundle-6320eef1.json"
}`,
			dir: "fuchsia",
		},
	}
	for _, test := range tests {
		t.Run(test.name, func(t *testing.T) {
			sink := newMemSink(contents, nil, test.dir)
			output, err := readAndUpdateProductBundle(ctx, sink, test.productBundlePath)
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
			sink := newMemSink(contents, test.dataSinkErr, test.dir)
			_, err := readAndUpdateProductBundle(ctx, sink, test.productBundlePath)
			if err != nil && err.Error() != test.expectedErrMessage {
				t.Errorf("Got error: %s, want: %s", err.Error(), test.expectedErrMessage)
			}
			if err == nil && test.expectedErrMessage != "" {
				t.Errorf("Got no error, want: %s", test.expectedErrMessage)
			}
		})
	}
}

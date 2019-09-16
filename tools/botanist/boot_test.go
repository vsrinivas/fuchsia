// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package botanist

import (
	"fmt"
	"io/ioutil"
	"os"
	"testing"

	"go.fuchsia.dev/fuchsia/tools/build/api"
)

func TestFilterZedbootShimImages(t *testing.T) {
	bootloaderImage := build.Image{
		Name:     "bootloader",
		PaveArgs: []string{"--bootloader"},
	}
	malformedImage := build.Image{
		Name:     "zircon-q",
		PaveArgs: []string{"--zirconq"},
	}
	signedZedbootImage := build.Image{
		Name:     "zedboot.signed",
		PaveArgs: []string{"--zirconr"},
	}
	zedbootImage := build.Image{
		Name:     "zedboot",
		PaveArgs: []string{"--zirconr"},
	}

	tests := []struct {
		name        string
		images      []build.Image
		expectedLen int
		expectedErr error
	}{
		{"Empty", []build.Image{}, 0, fmt.Errorf("no zircon-r image found in: %v", []build.Image{})},
		{"ZedbootNoBootloader", []build.Image{zedbootImage}, 1, nil},
		{"ZedbootAndBootloader", []build.Image{zedbootImage, bootloaderImage}, 2, nil},
		{"BooloaderNoZedboot", []build.Image{bootloaderImage}, 0, fmt.Errorf("no zircon-r image found in: %v", []build.Image{bootloaderImage})},
		{"SignedZedboot", []build.Image{signedZedbootImage}, 1, nil},
		{"UnexpectedArg", []build.Image{malformedImage}, 0, fmt.Errorf("unrecognized bootserver argument found: %q", "--zirconq")},
	}

	for _, test := range tests {
		for i := range test.images {
			tmpFile, err := ioutil.TempFile(os.TempDir(), test.images[i].Name)
			if err != nil {
				t.Fatalf("Failed to create tmp file: %s", err)
			}
			test.images[i].Path = tmpFile.Name()
			defer os.Remove(tmpFile.Name())
		}

		files, err := filterZedbootShimImages(test.images)

		if test.expectedErr != nil && err == nil {
			t.Errorf("Test%v: Exepected errors; no errors found", test.name)
		}

		if test.expectedErr == nil && err != nil {
			t.Errorf("Test%v: Exepected no errors; found error - %v", test.name, err)
		}

		if len(files) != test.expectedLen {
			t.Errorf("Test%v: Expected %d nodes; found %d", test.name, test.expectedLen, len(files))
		}
	}
}

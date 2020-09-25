// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"context"
	"io"
	"io/ioutil"
	"os"
	"path/filepath"
	"testing"

	"go.fuchsia.dev/fuchsia/tools/bootserver"
)

type mockReadAtCloser struct {
	contents string
	isClosed bool
}

func newMockReadAtCloser(contents string) *mockReadAtCloser {
	return &mockReadAtCloser{
		contents: contents,
		isClosed: false,
	}
}

func (m *mockReadAtCloser) ReadAt(_ []byte, _ int64) (int, error) {
	return len(m.contents), nil
}

func (m *mockReadAtCloser) Close() error {
	m.isClosed = true
	return nil
}

func TestOverrideImage(t *testing.T) {
	imgMap := map[string]bootserver.Image{}
	reader := newMockReadAtCloser("contents")
	bootloaderImg := bootserver.Image{
		Name:   "bootloader",
		Reader: reader,
	}
	imgMap = overrideImage(context.Background(), imgMap, bootloaderImg)
	if img := imgMap["bootloader"]; img.Reader != bootloaderImg.Reader {
		t.Errorf("expected %v, got %v", bootloaderImg, img)
	}
	if reader.isClosed {
		t.Errorf("reader was closed")
	}
	reader2 := newMockReadAtCloser("some other contents")
	bootloaderOverrideImg := bootserver.Image{
		Name:   "bootloader",
		Reader: reader2,
	}
	imgMap = overrideImage(context.Background(), imgMap, bootloaderOverrideImg)
	if img := imgMap["bootloader"]; img.Reader != bootloaderOverrideImg.Reader {
		t.Errorf("expected %v, got %v", bootloaderOverrideImg, img)
	}
	if !reader.isClosed {
		t.Errorf("failed to close old image reader")
	}
	if reader2.isClosed {
		t.Errorf("new image reader is closed")
	}
}

func TestPopulateReaders(t *testing.T) {
	bootloaderImage := bootserver.Image{
		Name: "bootloader",
		Args: []string{"--bootloader"},
	}
	firmwareImage := bootserver.Image{
		Name: "firmware",
		Args: []string{"--firmware"},
	}
	firmware2Image := bootserver.Image{
		Name: "firmware_2",
		Args: []string{"--firmware-2"},
	}
	vbmetaRImage := bootserver.Image{
		Name: "zedboot.vbmeta",
		Args: []string{"--vbmetar"},
	}
	zedbootImage := bootserver.Image{
		Name: "zedboot",
		Args: []string{"--zirconr"},
	}

	allImgs := []bootserver.Image{bootloaderImage, firmwareImage, firmware2Image, vbmetaRImage, zedbootImage}

	tests := []struct {
		name                 string
		existingImageIndexes []int
		expectErr            bool
	}{
		{"PopulateAllReaders", []int{0, 1, 2, 3, 4}, false},
		{"FileNotFound", []int{0}, true},
	}

	for _, test := range tests {
		testImgs := make([]bootserver.Image, len(allImgs))
		copy(testImgs, allImgs)
		tmpDir, err := ioutil.TempDir("", "test-data")
		if err != nil {
			t.Fatalf("failed to create temp dir: %v", err)
		}
		defer os.RemoveAll(tmpDir)
		for i := range testImgs {
			imgPath := filepath.Join(tmpDir, testImgs[i].Name)
			testImgs[i].Path = imgPath
		}

		for _, i := range test.existingImageIndexes {
			err := ioutil.WriteFile(testImgs[i].Path, []byte("data"), 0755)
			if err != nil {
				t.Fatalf("Failed to create tmp file: %s", err)
			}
		}

		closeFunc, err := populateReaders(testImgs)

		if test.expectErr && err == nil {
			t.Errorf("Test%v: Expected errors; no errors found", test.name)
		}

		if !test.expectErr && err != nil {
			t.Errorf("Test%v: Expected no errors; found error - %v", test.name, err)
		}

		if test.expectErr && err != nil {
			continue
		}
		for _, img := range testImgs {
			if img.Reader == nil {
				t.Errorf("Test%v: missing reader for %s", test.name, img.Name)
			}
			// The contents of each image is `data` so the size should be 4.
			if img.Size != int64(4) {
				t.Errorf("Test%v: incorrect size for %s; actual: %d, expected: 4", test.name, img.Name, img.Size)
			}
			buf := make([]byte, 1)
			if _, err := img.Reader.ReadAt(buf, 0); err != nil {
				t.Errorf("Test%v: failed to read %s: %v", test.name, img.Name, err)
			}
		}
		closeFunc()
		for _, img := range testImgs {
			buf := make([]byte, 1)
			if _, err := img.Reader.ReadAt(buf, 0); err == nil || err == io.EOF {
				t.Fatalf("Test%v: reader is not closed for %s", test.name, img.Name)
			}
		}
	}
}

// Checks that getFirmwareArgs(args) returns the expected arg names and types.
// To simplify usage, does not check for help text equality.
func expectGetFirmwareArgs(t *testing.T, args []string, expected []firmwareArg) {
	actual := getFirmwareArgs(args)

	if len(actual) != len(expected) {
		t.Errorf("Firmware length mismatch: expected %+v but got %+v", expected, actual)
	}

	for i := 0; i < len(actual); i++ {
		if actual[i].name != expected[i].name || actual[i].fwType != expected[i].fwType {
			t.Errorf("Firmware arg mismatch: expected %+v but got %+v", expected, actual)
			break
		}
	}
}

func TestGetFirmwareArgsNil(t *testing.T) {
	// Args always include the default "--firmware".
	expectGetFirmwareArgs(t,
		nil,
		[]firmwareArg{{name: "firmware", fwType: ""}})
}

func TestGetFirmwareArgsNoFirmware(t *testing.T) {
	expectGetFirmwareArgs(t,
		[]string{"--foo", "bar"},
		[]firmwareArg{{name: "firmware", fwType: ""}})
}

func TestGetFirmwareArgsUntypedFirmware(t *testing.T) {
	expectGetFirmwareArgs(t,
		[]string{"--firmware"},
		[]firmwareArg{{name: "firmware", fwType: ""}})
}

func TestGetFirmwareArgsTypedFirmware(t *testing.T) {
	expectGetFirmwareArgs(t,
		[]string{"--firmware-foo"},
		[]firmwareArg{
			{name: "firmware", fwType: ""},
			{name: "firmware-foo", fwType: "foo"},
		})
}

func TestGetFirmwareArgsSingleDash(t *testing.T) {
	expectGetFirmwareArgs(t,
		[]string{"-firmware-foo"},
		[]firmwareArg{
			{name: "firmware", fwType: ""},
			{name: "firmware-foo", fwType: "foo"},
		})
}

func TestGetFirmwareArgsWithEquals(t *testing.T) {
	expectGetFirmwareArgs(t,
		[]string{"--firmware-foo=bar"},
		[]firmwareArg{
			{name: "firmware", fwType: ""},
			{name: "firmware-foo", fwType: "foo"},
		})
}

func TestGetFirmwareArgsMultiple(t *testing.T) {
	expectGetFirmwareArgs(t,
		[]string{"abc", "--firmware", "fw1", "--firmware-2", "fw2", "skip_me", "-firmware-3=fw3"},
		[]firmwareArg{
			{name: "firmware", fwType: ""},
			{name: "firmware-2", fwType: "2"},
			{name: "firmware-3", fwType: "3"},
		})
}

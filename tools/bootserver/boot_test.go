// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package bootserver

import (
	"bytes"
	"context"
	"errors"
	"fmt"
	"io"
	"io/ioutil"
	"net"
	"path/filepath"
	"testing"
)

func TestDownloadImagesToDir(t *testing.T) {
	tmpDir := t.TempDir()
	var imgs []Image
	numImages := 4
	for i := 0; i < numImages; i++ {
		imgs = append(imgs, Image{
			Name:   fmt.Sprintf("image%d", i),
			Reader: bytes.NewReader([]byte(fmt.Sprintf("content of image%d", i))),
			Args:   []string{"--arg"},
		})
	}
	// Add another image without Args. This image should not be downloaded.
	imgs = append(imgs, Image{
		Name:   "noArgsImage",
		Reader: bytes.NewReader([]byte("content of noArgsImage")),
	})
	newImgs, closeFunc, err := downloadImagesToDir(context.Background(), tmpDir, imgs)
	if err != nil {
		t.Fatalf("failed to download image: %v", err)
	}
	defer closeFunc()
	if len(newImgs) != numImages {
		t.Errorf("unexpected number of images downloaded; expected: %d, actual: %d", numImages, len(newImgs))
	}
	for _, img := range newImgs {
		if img.Name == "noArgsImage" {
			t.Errorf("downloaded an image with no args")
		}
		content, err := ioutil.ReadFile(filepath.Join(tmpDir, img.Name))
		if err != nil {
			t.Fatalf("failed to read file: %v", err)
		}
		expectedData := fmt.Sprintf("content of %s", img.Name)
		if string(content) != expectedData {
			t.Errorf("unexpected content: expected: %s, actual: %s", expectedData, content)
		}
		if int(img.Size) != len(content) {
			t.Errorf("incorrect size: expected: %d, actual: %d", img.Size, len(content))
		}
	}
}

// A mock tftp.Client that supports setting expectations on Write() calls.
type mockTftpClient struct {
	// Testing class to mark any failures.
	t *testing.T

	// Expected files that should be written (in order), with the error to
	// return from the corresponding call to Write().
	expectedWrites []expectedWrite

	// Expected output from Read().
	expectedReads []string
}

type expectedWrite struct {
	// FTP filename to expect.
	filename string

	// Error to return from Write().
	ret error
}

func (c *mockTftpClient) Read(_ context.Context, _ string) (*bytes.Reader, error) {
	expected := c.expectedReads[0]
	c.expectedReads = c.expectedReads[1:]
	return bytes.NewReader([]byte(expected)), nil
}

func (c *mockTftpClient) RemoteAddr() *net.UDPAddr {
	c.t.Fatal("Unexpected call to mockTftpClient.RemoteAddr()")
	panic("notreached")
}

func (c *mockTftpClient) Write(_ context.Context, filename string, _ io.ReaderAt, _ int64) error {
	if len(c.expectedWrites) == 0 {
		c.t.Fatalf("No writes expected but got %q", filename)
		panic("notreached")
	}

	expected := c.expectedWrites[0]
	if expected.filename != filename {
		c.t.Fatalf("Expected %q but got %q", expected.filename, filename)
		panic("notreached")
	}

	c.expectedWrites = c.expectedWrites[1:]
	return expected.ret
}

// Creates a test Image with the given bootserver arg and small contents.
func testImage(arg string) Image {
	return Image{
		Reader: bytes.NewReader([]byte(fmt.Sprintf("image contents for %q arg", arg))),
		Args:   []string{arg},
	}
}

// Creates Images based on imageArgs, then calls transferImages() and verifies
// that the expected writes were called.
func validateTransferImages(t *testing.T, imageArgs []string, expectedWrites []expectedWrite) {
	// Convert the bootserver args to test Images, contents don't matter.
	images := []Image{}
	for _, arg := range imageArgs {
		images = append(images, testImage(arg))
	}
	client := mockTftpClient{t, expectedWrites, []string{}}

	_, err := transferImages(context.Background(), &client, images, nil, nil)
	if err != nil {
		t.Errorf("transferImages() failed: %v", err)
	}

	// Make sure all the expected writes were consumed.
	if len(client.expectedWrites) > 0 {
		t.Errorf("Expected writes were never made: %+v\n", client.expectedWrites)
	}
}

func TestTransferImagesZirconA(t *testing.T) {
	validateTransferImages(
		t,
		[]string{"--zircona"},
		[]expectedWrite{{filename: "<<image>>zircona.img"}},
	)
}

func TestTransferImagesUntypedFirmware(t *testing.T) {
	validateTransferImages(
		t,
		[]string{"--firmware"},
		[]expectedWrite{{filename: "<<image>>firmware_"}},
	)
}

func TestTransferImagesUntypedFirmwareTrailingDash(t *testing.T) {
	validateTransferImages(
		t,
		[]string{"--firmware-"},
		[]expectedWrite{{filename: "<<image>>firmware_"}},
	)
}

func TestTransferImagesTypedFirmware(t *testing.T) {
	validateTransferImages(
		t,
		[]string{"--firmware-foo"},
		[]expectedWrite{{filename: "<<image>>firmware_foo"}},
	)
}

func TestTransferImagesOrdering(t *testing.T) {
	validateTransferImages(
		t,
		[]string{
			"--vbmetab",
			"--zircona",
			"--firmware-foo",
			"--vbmetaa",
			"--firmware",
			"--zirconb",
		},
		[]expectedWrite{
			{filename: "<<image>>firmware_"},
			{filename: "<<image>>firmware_foo"},
			{filename: "<<image>>zircona.img"},
			{filename: "<<image>>zirconb.img"},
			{filename: "<<image>>vbmetaa.img"},
			{filename: "<<image>>vbmetab.img"},
		},
	)
}

func TestTransferImagesSkipFirmwareFailure(t *testing.T) {
	// Transfer should skip a failed firmware write and continue to send
	// the remaining images.
	validateTransferImages(
		t,
		[]string{
			"--firmware",
			"--zircona",
			"--zirconb",
		},
		[]expectedWrite{
			{filename: "<<image>>firmware_", ret: errors.New("expected failure")},
			{filename: "<<image>>zircona.img"},
			{filename: "<<image>>zirconb.img"},
		},
	)
}

func TestValidateBoard(t *testing.T) {
	for _, test := range []struct {
		name         string
		board        string
		expectedRead string
		wantErr      bool
	}{
		{
			name:         "board is valid",
			board:        "x64",
			expectedRead: "x64",
			wantErr:      false,
		},
		{
			name:         "null-terminated board is valid",
			board:        "x64",
			expectedRead: "x64\x00",
			wantErr:      false,
		},
		{
			name:         "board is invalid",
			board:        "x64",
			expectedRead: "arm64",
			wantErr:      true,
		},
	} {
		t.Run(test.name, func(t *testing.T) {
			client := &mockTftpClient{t: t, expectedReads: []string{test.expectedRead}}
			err := ValidateBoard(context.Background(), client, test.board)
			if test.wantErr != (err != nil) {
				t.Errorf("failed to validate board; want err: %v, err: %v", test.wantErr, err)
			}
		})
	}
}

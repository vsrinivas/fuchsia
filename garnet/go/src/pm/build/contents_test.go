// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package build

import (
	"fmt"
	"reflect"
	"strings"
	"testing"
)

func TestMerkleString(t *testing.T) {
	merkle := MerkleRoot{
		0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff,
		0xff, 0xee, 0xdd, 0xcc, 0xbb, 0xaa, 0x99, 0x88, 0x77, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11, 0x00,
	}
	expected := "00112233445566778899aabbccddeeffffeeddccbbaa99887766554433221100"
	actual := merkle.String()
	if expected != actual {
		t.Errorf("got %v, want %v", actual, expected)
	}
}

func TestDecodeMerkle(t *testing.T) {
	expected := MerkleRoot{
		0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff,
		0xff, 0xee, 0xdd, 0xcc, 0xbb, 0xaa, 0x99, 0x88, 0x77, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11, 0x00,
	}
	merkle, err := DecodeMerkleRoot([]byte("00112233445566778899aabbccddeeffffeeddccbbaa99887766554433221100"))
	if err != nil {
		t.Errorf("Error decoding merkle: %v", err)
	}
	if merkle != expected {
		t.Errorf("got %v, want %v", merkle, expected)
	}
}

func TestDecodeMerkle_shortEven(t *testing.T) {
	_, err := DecodeMerkleRoot([]byte("00112233445566778899aabbccddeeffffeeddccbbaa998877665544332211"))
	if err != ErrInvalidMerkleRootLength {
		t.Errorf("got %v, want %v", err, ErrInvalidMerkleRootLength)
	}
}

func TestDecodeMerkle_longOdd(t *testing.T) {
	merkle, err := DecodeMerkleRoot([]byte("00112233445566778899aabbccddeeffffeeddccbbaa99887766554433221100f"))
	if err == nil {
		t.Errorf("got %v, want an error", merkle)
	}
}

func verifyMetaContents(t *testing.T, expected MetaContents, source string) {
	contents, err := ParseMetaContents(strings.NewReader(source))
	if err != nil {
		t.Fatalf("unexpected parse error: %v", err)
	}

	if !reflect.DeepEqual(contents, expected) {
		t.Errorf("got %v, want %v", contents, expected)
	}
}

func verifyInvalidMetaContents(t *testing.T, source string) {
	contents, err := ParseMetaContents(strings.NewReader(source))
	if err == nil {
		t.Errorf("expected parse error, got %v", contents)
	}
}

const (
	merkleA = "00112233445566778899aabbccddeeffffeeddccbbaa99887766554433221100"
	merkleE = "eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee"
	merkleF = "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
)

// Verify that ParseMetaContents can read the manifest file format.
func TestParseMetaContents(t *testing.T) {
	expected := MetaContents{
		"bin/app": MustDecodeMerkleRoot(merkleA),
		"foo/bar": MustDecodeMerkleRoot(merkleF),
	}
	verifyMetaContents(t, expected,
		fmt.Sprintf("bin/app=%s\nfoo/bar=%s", merkleA, merkleF))

	verifyMetaContents(t, expected,
		fmt.Sprintf("bin/app=%s\nfoo/bar=%s\n", merkleA, merkleF))
}

// Blank lines and lines containing only whitespace are not valid.
func TestParseMetaContents_InvalidLine(t *testing.T) {
	verifyInvalidMetaContents(t, "\n\n")
	verifyInvalidMetaContents(t, fmt.Sprintf("\nbin/app=%s", merkleA))
	verifyInvalidMetaContents(t, fmt.Sprintf(" \nbin/app=%s", merkleA))
	verifyInvalidMetaContents(t, fmt.Sprintf("foo/bar=%s\n\nbin/app=%s", merkleE, merkleF))
	verifyInvalidMetaContents(t, fmt.Sprintf("foo/bar=%s\n \nbin/app=%s", merkleE, merkleF))
	verifyInvalidMetaContents(t, fmt.Sprintf("bin/app=%s\n\n", merkleA))
	verifyInvalidMetaContents(t, fmt.Sprintf("bin/app=%s\n ", merkleA))

	// However, a single empty line at the end of a manifest is allowed.
	verifyMetaContents(t,
		MetaContents{"foo": MustDecodeMerkleRoot(merkleA)},
		fmt.Sprintf("foo=%s\n", merkleA))
}

// Verify that String() produces the expected manifest file format.
func TestMetaContentsString(t *testing.T) {
	contents := MetaContents{
		"bin/app": MustDecodeMerkleRoot(merkleA),
		"foo/bar": MustDecodeMerkleRoot(merkleE),
	}
	actual := contents.String()

	expected := fmt.Sprintf("bin/app=%s\nfoo/bar=%s\n", merkleA, merkleE)

	if expected != actual {
		t.Errorf("got %v, want %v", actual, expected)
	}
}

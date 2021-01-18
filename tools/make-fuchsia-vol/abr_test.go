// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
package main

import (
	"bytes"
	"testing"
)

func assertArrayEq(t *testing.T, result []byte, expected []byte) {
	if len(result) != len(expected) {
		t.Fatalf("test output is incorrect length!\nExpected: %d\nGot: %d\n", len(expected), len(result))
	}

	ok := true
	for i := range result {
		if result[i] != expected[i] {
			t.Errorf("expected and result differ at index %d. Expected %d, got %d\n", i, expected[i], result[i])
			ok = false
		}
	}

	if !ok {
		t.FailNow()
	}
}

func TestWriteAbrA(t *testing.T) {
	var buffer bytes.Buffer

	WriteAbr(BOOT_A, &buffer)

	expected := []byte{
		// magic
		0, 'A', 'B', '0',
		// version_major, version_minor, reserved x2
		2, 1, 0, 0,
		// slot 1 data
		// priority, tries_remaining, successful_boot, reserved
		abrMaxPriority, abrMaxTriesRemaining, 0, 0,
		// slot 2 data
		// priority, tries_remaining, successful_boot, reserved
		1, abrMaxTriesRemaining, 0, 0,
		// one_shot_recovery_boot, reserved x11
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		// crc32 of the previous 28 bytes in big endian.
		52, 118, 193, 121,
	}

	var result [32]byte
	n, err := buffer.Read(result[:])
	if err != nil {
		t.Fatal(err)
	}

	assertArrayEq(t, result[0:n], expected)
}

func TestWriteAbrB(t *testing.T) {
	var buffer bytes.Buffer

	WriteAbr(BOOT_B, &buffer)

	expected := []byte{
		// magic
		0, 'A', 'B', '0',
		// version_major, version_minor, reserved x2
		2, 1, 0, 0,
		// slot 1 data
		// priority, tries_remaining, successful_boot, reserved
		1, abrMaxTriesRemaining, 0, 0,
		// slot 2 data
		// priority, tries_remaining, successful_boot, reserved
		abrMaxPriority, abrMaxTriesRemaining, 0, 0,
		// one_shot_recovery_boot, reserved x11
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		// crc32 of the previous 28 bytes in big endian.
		13, 12, 124, 181,
	}

	var result [32]byte
	n, err := buffer.Read(result[:])
	if err != nil {
		t.Fatal(err)
	}

	assertArrayEq(t, result[0:n], expected)
}

func TestWriteAbrRecovery(t *testing.T) {
	var buffer bytes.Buffer

	WriteAbr(BOOT_RECOVERY, &buffer)

	expected := []byte{
		// magic
		0, 'A', 'B', '0',
		// version_major, version_minor, reserved x2
		2, 1, 0, 0,
		// slot 1 data
		// priority, tries_remaining, successful_boot, reserved
		0, abrMaxTriesRemaining, 0, 0,
		// slot 2 data
		// priority, tries_remaining, successful_boot, reserved
		0, abrMaxTriesRemaining, 0, 0,
		// one_shot_recovery_boot, reserved x11
		0, 0, 0, 0,
		0, 0, 0, 0,
		0, 0, 0, 0,
		// crc32 of the previous 28 bytes in big endian.
		230, 129, 32, 201,
	}

	var result [32]byte
	n, err := buffer.Read(result[:])
	if err != nil {
		t.Fatal(err)
	}

	assertArrayEq(t, result[0:n], expected)
}

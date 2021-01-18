// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This is a very basic library to write out ABR boot partitions in the format described
// at //src/firmware/lib/abr/include/lib/abr/data.h
package main

import (
	"bytes"
	"encoding/binary"
	"hash/crc32"
	"io"
	"unsafe"
)

type BootPartition int

const (
	BOOT_A BootPartition = iota
	BOOT_B
	BOOT_RECOVERY
)

const (
	abrMaxPriority       = 15
	abrMaxTriesRemaining = 7
)

func AbrMagic() [4]uint8 {
	return [4]uint8{0, 'A', 'B', '0'}
}

type AbrSlotData struct {
	priority        uint8
	tries_remaining uint8
	successful_boot uint8
	reserved        [1]uint8
}

type AbrData struct {
	magic [4]uint8

	version_major uint8
	version_minor uint8

	reserved1 [2]uint8

	slot_data [2]AbrSlotData

	one_shot_recovery_boot uint8

	reserved2 [11]uint8

	crc uint32
}

func makeAbrHeader(partition BootPartition) (*AbrData, error) {
	data := &AbrData{
		magic:         AbrMagic(),
		version_major: 2,
		version_minor: 1,
	}

	// Give both slots max tries remaining.
	data.slot_data[0].tries_remaining = abrMaxTriesRemaining
	data.slot_data[1].tries_remaining = abrMaxTriesRemaining
	// Figure out how we should boot. We mark both slots as bootable,
	// but set the selected slot to have the highest priority.
	switch partition {
	case BOOT_A:
		data.slot_data[0].priority = abrMaxPriority
		data.slot_data[1].priority = 1
	case BOOT_B:
		data.slot_data[1].priority = abrMaxPriority
		data.slot_data[0].priority = 1
	case BOOT_RECOVERY:
		// Mark both slots as unbootable.
		data.slot_data[0].priority = 0
		data.slot_data[1].priority = 0
	}

	var buffer bytes.Buffer

	err := binary.Write(&buffer, binary.BigEndian, data)
	if err != nil {
		return nil, err
	}

	// Remove the 4 CRC bytes from the CRC calculation.
	buffer.Truncate(int(unsafe.Sizeof(AbrData{}) - 4))

	data.crc = crc32.ChecksumIEEE(buffer.Bytes())

	return data, nil
}

func WriteAbr(partition BootPartition, file io.Writer) error {
	data, err := makeAbrHeader(partition)
	if err != nil {
		return err
	}

	return binary.Write(file, binary.BigEndian, data)
}

// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package eapol

import (
	"fmt"
)

// 'KeyDataReader' is an implementation of a KDE and Element parser for EAPoL Key frames' data
// field. The parser refers to KDEs and Elements as 'Items'.

type byteDeserializer interface {
	deserialize(data []uint8) error
}

const kdeType = uint8(0xdd)

var KDE_OUI = [3]uint8{0x00, 0x0F, 0xAC}

const (
	KeyDataItemType_Unknown KeyDataItemType = iota
	KeyDataItemType_KDE
	KeyDataItemType_Element
)

type KeyDataItemType uint8

type ElementID uint8

type KDEType uint8

const kdeHdrLenBytes = 6
const elemHdrLenBytes = 2

const (
	KDEType_GTK KDEType = 1
)

// IEEE Std 802.11-2016 12.7.2, Figure 12-34
type KDEHeader struct {
	Type     KDEType
	Length   uint8
	OUI      [3]uint8
	DataType KDEType
}

// IEEE Std 802.11-2016 12.7.2, Figure 12-35
type GTKKDE struct {
	KDEHeader
	KeyID uint8
	Tx    bool
	GTK   []uint8
}

const (
	ElementID_Rsn ElementID = 48
)

// IEEE Std 802.11-2016, 9.4.2.1
type ElementHeader struct {
	Id     ElementID
	Length uint8
}

// RSNE is only compared to the selected RSNE from association request. Bytes can be compared. No
// need for further decoding.
type RSNElement struct {
	Raw []byte
}

type KeyDataReader struct {
	data   []uint8
	offset uint32
}

func NewKeyDataReader(data []uint8) *KeyDataReader {
	return &KeyDataReader{
		data:   data,
		offset: 0,
	}
}

func (reader *KeyDataReader) CanRead() bool {
	return reader.offset < uint32(len(reader.data))
}

func (reader *KeyDataReader) PeekType() KeyDataItemType {
	if !reader.CanRead() {
		return KeyDataItemType_Unknown
	}
	if reader.data[reader.offset] == kdeType {
		return KeyDataItemType_KDE
	}
	return KeyDataItemType_Element
}

func (reader *KeyDataReader) PeekKDE() *KDEHeader {
	if reader.PeekType() != KeyDataItemType_KDE {
		return nil
	}
	hdr := &KDEHeader{}
	hdr.deserialize(reader.data[reader.offset:])
	return hdr
}

func (reader *KeyDataReader) PeekElement() *ElementHeader {
	if reader.PeekType() != KeyDataItemType_Element {
		return nil
	}
	hdr := &ElementHeader{}
	hdr.deserialize(reader.data[reader.offset:])
	return hdr
}

func (reader *KeyDataReader) ReadKDE(kde byteDeserializer) error {
	hdr := reader.PeekKDE()
	if hdr == nil {
		return fmt.Errorf("byte stream does not represent a KDE")
	}
	// Ensure the reader makes progress.
	dataLen := getKDEDataLength(reader.data)
	defer func() { reader.offset += uint32(kdeHdrLenBytes + dataLen) }()

	if kde != nil {
		if err := kde.deserialize(reader.data[reader.offset:]); err != nil {
			return err
		}
	}

	// KDEs with a length of zero indicate key wrap padding and thus end of parsing
	if hdr.Length == 0 {
		reader.offset = uint32(len(reader.data))
	}
	return nil
}

func (reader *KeyDataReader) ReadElement(elem byteDeserializer) error {
	hdr := reader.PeekElement()
	if hdr == nil {
		return fmt.Errorf("byte stream does not represent an Element")
	}
	// Ensure the reader makes progress.
	defer func() { reader.offset += uint32(elemHdrLenBytes + hdr.Length) }()

	if elem != nil {
		if err := elem.deserialize(reader.data[reader.offset:]); err != nil {
			return err
		}
	}
	return nil
}

func getKDEDataLength(rawKDE []uint8) uint8 {
	if len(rawKDE) < 2 {
		return 0
	}
	// In KDEs the last four bytes of the header are part of its length.
	length := rawKDE[1]
	if length < 4 {
		return 0
	}
	return length - 4
}

func (hdr *KDEHeader) deserialize(data []uint8) error {
	if len(data) < kdeHdrLenBytes {
		return fmt.Errorf("KDE too short")
	}
	length := data[1]
	if getKDEDataLength(data) == 0 || int(length) > len(data)-2 {
		return fmt.Errorf("KDE too short")
	}

	hdr.Type = KDEType(data[0])
	hdr.Length = data[1]
	copy(hdr.OUI[:], data[2:])
	hdr.DataType = KDEType(data[5])
	return nil
}

func (hdr *ElementHeader) deserialize(data []uint8) error {
	if len(data) < elemHdrLenBytes {
		return fmt.Errorf("Element too short")
	}
	length := data[1]
	if length > uint8(len(data))-elemHdrLenBytes {
		return fmt.Errorf("Element too short")
	}

	hdr.Id = ElementID(data[0])
	hdr.Length = length
	return nil
}

func (kde *GTKKDE) deserialize(data []uint8) error {
	if err := kde.KDEHeader.deserialize(data); err != nil {
		return err
	}

	offset := kdeHdrLenBytes
	kde.KeyID = data[offset] & 0x3     // bit 0-1
	kde.Tx = (data[offset] & 0x4) != 0 // bit 2
	kde.GTK = make([]byte, kde.Length-kdeHdrLenBytes)
	copy(kde.GTK, data[offset+2:])
	return nil
}

func (elem *RSNElement) deserialize(data []uint8) error {
	hdr := &ElementHeader{}
	if err := hdr.deserialize(data); err != nil {
		return err
	}
	elem.Raw = make([]byte, hdr.Length+elemHdrLenBytes)
	copy(elem.Raw, data[:len(elem.Raw)])
	return nil
}

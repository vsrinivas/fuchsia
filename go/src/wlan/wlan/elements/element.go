// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package elements

import (
	"bytes"
	"encoding/binary"
	"fmt"
	"io"
)

// IEEE Std 802.11-2016, 9.4.2.1 Table 9-77

type Id uint8

const (
	RSNId Id = 48
)

// IEEE Std 802.11-2016, 9.4.2.1
type Header struct {
	Id  Id
	Len uint8

	// Element ID Extension (0 or 1)
	// Information (variable)
}

// IEEE Std 802.11-2016, 9.4.2.25.2, Table 9-131

type CipherSuiteType uint8

const (
	CipherSuiteType_GroupCipherSuite CipherSuiteType = 0
	CipherSuiteType_WEP40                            = 1
	CipherSuiteType_TKIP                             = 2
	// 3 Reserved
	CipherSuiteType_CCMP128                         = 4
	CipherSuiteType_WEP104                          = 5
	CipherSuiteType_BIP_CMAC128                     = 6
	CipherSuiteType_GroupAddressedTrafficNotAllowed = 7
	CipherSuiteType_GCMP128                         = 8
	CipherSuiteType_GCMP256                         = 9
	CipherSuiteType_CCMP256                         = 10
	CipherSuiteType_BIP_GMAC128                     = 11
	CipherSuiteType_BIP_GMAC256                     = 12
	CipherSuiteType_BIP_CMAC256                     = 13
	// 14 - 255 Reserved
)

// IEEE Std 802.11-2016, 9.4.2.25.2, Table 9-133

type AKMSuiteType uint8

const (
	// 0 Reserved
	AkmSuiteType_8021X            AKMSuiteType = 1
	AkmSuiteType_PSK                           = 2
	AkmSuiteType_FT_8021X                      = 3
	AkmSuiteType_FT_PSK                        = 4
	AkmSuiteType_8021X_SHA256                  = 5
	AkmSuiteType_PSK_SHA256                    = 6
	AkmSuiteType_TDLS                          = 7
	AkmSuiteType_SAE                           = 8
	AkmSuiteType_FT_SAE                        = 9
	AkmSuiteType_ApPeerKey                     = 10
	AkmSuiteType_8021X_EAP_SHA256              = 11
	AkmSuiteType_8021X_SHA384                  = 12
	AkmSuiteType_FT_8021X_SHA384               = 13
	// 14 - 255 Reserved
)

type CipherSuiteOUI [3]uint8

// IEEE Std 802.11-2016, 9.4.2.25.2
type CipherSuite struct {
	OUI  CipherSuiteOUI
	Type CipherSuiteType
}

// IEEE Std 802.11-2016, 9.4.2.25.3
type AKMSuite struct {
	OUI  CipherSuiteOUI
	Type AKMSuiteType
}

type PMKID [16]uint8

var DefaultCipherSuiteOUI = CipherSuiteOUI{0x00, 0x0F, 0xAC}
var DefaultRSNEVersion uint16 = 1

type RSN struct {
	Hdr             Header
	Version         uint16
	GroupData       *CipherSuite
	PairwiseCiphers []CipherSuite
	AKMs            []AKMSuite
	// TODO(hahnr): Add bitfield support (IEEE Std 802.11-2016, 9.4.2.25.4, Figure 9-257).
	Caps      *uint16
	PMKIDs    []PMKID
	GroupMgmt *CipherSuite
}

func NewEmptyRSN() (rsn *RSN) {
	return &RSN{
		Hdr:     Header{Id: RSNId},
		Version: DefaultRSNEVersion,
	}
}

func ParseRSN(raw []uint8) (rsn *RSN, e error) {
	// Adjust a possibly incorrect element length when parsing finished.
	reader := bytes.NewReader(raw)
	defer func() {
		if rsn != nil {
			writtenBytes := len(raw) - reader.Len() // total - unread bytes
			rsn.Hdr = Header{RSNId, uint8(writtenBytes)}
		}
	}()

	// Must be at least 4 bytes (Element Header + Version)
	if reader.Len() < 4 || Id(raw[0]) != RSNId {
		return nil, fmt.Errorf("Invalid RSN element")
	}

	rsn = &RSN{}
	reader.Seek(2, io.SeekCurrent) // Skip Element Header
	binary.Read(reader, binary.LittleEndian, &rsn.Version)

	// Group Data Cipher Suite
	if reader.Len() < 4 {
		return
	}
	rsn.GroupData = &CipherSuite{}
	binary.Read(reader, binary.LittleEndian, rsn.GroupData)

	// Pairwise cipher Suite
	var count uint16
	if err := binary.Read(reader, binary.LittleEndian, &count); err != nil {
		return
	}
	if count == 0 || reader.Len() < int(count)*4 {
		return
	}
	rsn.PairwiseCiphers = make([]CipherSuite, count)
	binary.Read(reader, binary.LittleEndian, &rsn.PairwiseCiphers)

	// AKM cipher Suite
	if err := binary.Read(reader, binary.LittleEndian, &count); err != nil {
		return
	}
	if count == 0 || reader.Len() < int(count)*4 {
		return
	}
	rsn.AKMs = make([]AKMSuite, count)
	binary.Read(reader, binary.LittleEndian, &rsn.AKMs)

	// RSN Capabilities
	if reader.Len() < 2 {
		return
	}
	rsn.Caps = new(uint16)
	if err := binary.Read(reader, binary.LittleEndian, rsn.Caps); err != nil {
		return
	}

	// PMKIDs
	if err := binary.Read(reader, binary.LittleEndian, &count); err != nil {
		return
	}
	if count == 0 || reader.Len() < int(count)*16 {
		return
	}
	rsn.PMKIDs = make([]PMKID, count)
	binary.Read(reader, binary.LittleEndian, &rsn.PMKIDs)

	// Group Management Cipher Suite
	if reader.Len() < 4 {
		return
	}
	rsn.GroupMgmt = &CipherSuite{}
	binary.Read(reader, binary.LittleEndian, rsn.GroupMgmt)
	return
}

func (r *RSN) Bytes() (result []byte) {
	// Adjust element length with actual written bytes.
	buf := bytes.Buffer{}
	defer func() {
		result = buf.Bytes()
		result[1] = uint8(len(result) - 2)
	}()

	binary.Write(&buf, binary.LittleEndian, RSNId)
	binary.Write(&buf, binary.LittleEndian, r.Hdr.Len)
	binary.Write(&buf, binary.LittleEndian, r.Version)

	// Group Data Cipher Suite
	if r.GroupData == nil {
		return
	}
	binary.Write(&buf, binary.LittleEndian, r.GroupData)

	// Pairwise cipher Suite
	if len(r.PairwiseCiphers) == 0 {
		return
	}
	binary.Write(&buf, binary.LittleEndian, uint16(len(r.PairwiseCiphers)))
	binary.Write(&buf, binary.LittleEndian, r.PairwiseCiphers)

	// AKM cipher Suite
	if len(r.AKMs) == 0 {
		return
	}
	binary.Write(&buf, binary.LittleEndian, uint16(len(r.AKMs)))
	binary.Write(&buf, binary.LittleEndian, r.AKMs)

	// RSN Capabilities
	if r.Caps == nil {
		return
	}
	binary.Write(&buf, binary.LittleEndian, *r.Caps)

	// PMKIDs
	if len(r.PMKIDs) == 0 {
		return
	}
	binary.Write(&buf, binary.LittleEndian, uint16(len(r.PMKIDs)))
	binary.Write(&buf, binary.LittleEndian, r.PMKIDs)

	// Group Management Cipher Suite
	if r.GroupMgmt == nil {
		return
	}
	binary.Write(&buf, binary.LittleEndian, r.GroupMgmt)
	return
}

// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package eapol

import (
	"wlan/wlan/sme"
	mlme "garnet/public/lib/wlan/fidl/wlan_mlme"

	"bytes"
	"encoding/binary"
	"fmt"
	"io"
	"crypto/hmac"
	"crypto/sha1"
)

// IEEE Std 802.1X-2010, 11.3.2, Table 11-3
const (
	PacketType_EAP    = 0
	PacketType_Start  = 1
	PacketType_Logoff = 2
	PacketType_Key    = 3
	// TODO(hahnr): Add remaining types ones the need arises.
)

// IEEE Std 802.11-2016, 12.7.2, Figure 12-33
type KeyInfo uint16 // Bitmask

func (k KeyInfo) IsSet(test KeyInfo) bool { return k&test != 0 }

func (k KeyInfo) Extract(mask KeyInfo) uint16 { return uint16(k & mask) }

func (k KeyInfo) Update(clear KeyInfo, set KeyInfo) KeyInfo {
	return k & ^clear | set
}

const (
	KeyInfo_DescriptorVersion KeyInfo = 7 // Bit 0-2
	KeyInfo_Type              KeyInfo = 1 << 3
	// Bit 4-5 Reserved
	KeyInfo_Install           KeyInfo = 1 << 6
	KeyInfo_ACK               KeyInfo = 1 << 7
	KeyInfo_MIC               KeyInfo = 1 << 8
	KeyInfo_Secure            KeyInfo = 1 << 9
	KeyInfo_Error             KeyInfo = 1 << 10
	KeyInfo_Request           KeyInfo = 1 << 11
	KeyInfo_Encrypted_KeyData KeyInfo = 1 << 12
	KeyInfo_SMK_Message       KeyInfo = 1 << 13
	// Bit 14-15 Reserved
)

// IEEE Std 802.1X-2010, 11.3, Figure 11-1
type Header struct {
	Version          uint8
	PacketType       uint8
	PacketBodyLength uint16
}

const HeaderLen = 4

// IEEE Std 802.11-2016, 12.7.2, Figure 12-32
type KeyFrame struct {
	Header

	DescriptorType uint8
	Info           KeyInfo
	Length         uint16
	ReplayCounter  [8]uint8
	Nonce          [32]uint8
	IV             [16]uint8
	RSC            [8]uint8
	MIC            []uint8 // Size based on AKM
	DataLength     uint16
	Data           []uint8
}

// MIC is 16 octets, and 24 octets for AKM 12 & 13. This constant represents the minimum length of
// an EAPOL Key Frame packet body excluding the dynamic MIC.
// IEEE Std 802.11-2016, 12.7.3, Table 12-8
const KeyFrameBodyMinLenExclusiveMIC = 79

func NewEmptyKeyFrame(micBits int) *KeyFrame {
	return &KeyFrame{
		MIC: make([]uint8, micBits/8),
	}
}

func ParseHeader(raw []byte) (hdr *Header, err error) {
	// Must be at least 4 bytes to fit Header
	reader := bytes.NewReader(raw)
	if reader.Len() < HeaderLen {
		return nil, fmt.Errorf("Invalid EAPOL header")
	}

	hdr = &Header{}
	binary.Read(reader, binary.BigEndian, hdr)
	return
}

func ParseKeyFrame(hdr *Header, body []byte, micBits int) (*KeyFrame, error) {
	if hdr == nil {
		return nil, fmt.Errorf("Header mustn't be nil")
	}
	if hdr.PacketType != PacketType_Key {
		return nil, fmt.Errorf("not an EAPOL Key frame, type: %d", hdr.PacketType)
	}
	if hdr.PacketBodyLength < uint16(KeyFrameBodyMinLenExclusiveMIC+micBits/8) {
		return nil, fmt.Errorf("short EAPOL Key frame, length: %d", hdr.PacketBodyLength)
	}
	frame := &KeyFrame{}
	frame.Header = *hdr

	// Read field by field rather than the struct all at once due to the 8 reserved bytes.
	reader := bytes.NewReader(body)
	binary.Read(reader, binary.BigEndian, &frame.DescriptorType)
	binary.Read(reader, binary.BigEndian, &frame.Info)
	binary.Read(reader, binary.BigEndian, &frame.Length)
	binary.Read(reader, binary.BigEndian, &frame.ReplayCounter)
	binary.Read(reader, binary.BigEndian, &frame.Nonce)
	binary.Read(reader, binary.BigEndian, &frame.IV)
	binary.Read(reader, binary.BigEndian, &frame.RSC)
	reader.Seek(8, io.SeekCurrent) // 8 bytes reserved
	frame.MIC = make([]byte, micBits/8)
	binary.Read(reader, binary.BigEndian, &frame.MIC)
	binary.Read(reader, binary.BigEndian, &frame.DataLength)

	// Read body and ensure correct length.
	if reader.Len() != int(frame.DataLength) {
		return nil, fmt.Errorf("corrupted EAPOL frame body, expected %d bytes but had %d remaining", hdr.PacketBodyLength, reader.Len())
	}
	frame.Data = make([]byte, reader.Len())
	binary.Read(reader, binary.BigEndian, &frame.Data)
	return frame, nil
}

func (f *KeyFrame) Bytes() []uint8 {
	// We have to insert 8 reserved bytes and hence have to write field by field, rather than the struct all at once.
	// Enforce correct packet body length by always updating it.
	f.Header.PacketBodyLength = uint16(KeyFrameBodyMinLenExclusiveMIC + len(f.MIC) + len(f.Data))
	buf := bytes.Buffer{}
	binary.Write(&buf, binary.BigEndian, f.Header)
	binary.Write(&buf, binary.BigEndian, f.DescriptorType)
	binary.Write(&buf, binary.BigEndian, f.Info)
	binary.Write(&buf, binary.BigEndian, f.Length)
	binary.Write(&buf, binary.BigEndian, f.ReplayCounter)
	binary.Write(&buf, binary.BigEndian, f.Nonce)
	binary.Write(&buf, binary.BigEndian, f.IV)
	binary.Write(&buf, binary.BigEndian, f.RSC)
	binary.Write(&buf, binary.BigEndian, make([]byte, 8))
	binary.Write(&buf, binary.BigEndian, f.MIC)
	binary.Write(&buf, binary.BigEndian, uint16(len(f.Data))) // Derive DataLength from Data
	binary.Write(&buf, binary.BigEndian, f.Data)
	return buf.Bytes()
}

func (f *KeyFrame) HasValidMIC(kck []byte) bool {
	mic := computeMIC(kck, f)
	return bytes.Compare(mic, f.MIC) == 0
}

func (f *KeyFrame) UpdateMIC(kck []byte) {
	f.MIC = computeMIC(kck, f)
}

// IEEE Std 802.11-2016, 12.7.2 h)
func computeMIC(kck []byte, f *KeyFrame) []byte {
	if len(f.MIC) == 0 {
		panic("MIC size is zero. Did you forget to initialize MIC?")
	}

	// Frame's MIC must be set to zero for computing the MIC.
	mic := make([]byte, len(f.MIC))
	copy(mic, f.MIC)
	f.MIC = make([]byte, len(f.MIC))
	defer func() { copy(f.MIC, mic) }() // Restore MIC when done.
	// Integrity algorithm depends on the negotiated AKM. Only AKM-2 is supported for now.
	// IEEE Std 802.11-2016, 12.7.3 Table 12-8
	hsha1 := hmac.New(sha1.New, kck)
	binary.Write(hsha1, binary.BigEndian, f.Bytes())
	return hsha1.Sum(nil)[:len(kck)]
}

// Responsible for processing incoming EAPOL frames and computing the KEK, KCK and TK. Note, that
// none of the incoming EAPOL Key frames was checked or filtered for correctness. It's the
// implementation's responsibility to verify the frame's correctness, e.g., verifying KeyInfo & MIC.
// TODO(hahnr): Evaluate whether we need a more granular component separation. E.g., to split up
// authentication, and key derivation.
type KeyExchange interface {
	HandleEAPOLKeyFrame(f *KeyFrame) error
}

// Transports EAPOL frames to their destination.
type Transport interface {
	SendEAPOLKeyFrame(srcAddr [6]uint8, dstAddr [6]uint8, f *KeyFrame) error
}

// Sends EAPOL frames via SME.
type SMETransport struct {
	SME sme.Transport
}

func (s *SMETransport) SendEAPOLKeyFrame(srcAddr [6]uint8, dstAddr [6]uint8, f *KeyFrame) error {
	req := &mlme.EapolRequest{
		SrcAddr: srcAddr,
		DstAddr: dstAddr,
		Data:	f.Bytes(),
	}
	s.SME.SendMessage(req, int32(mlme.Method_EapolRequest))
	return nil
}

// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package handshake

import (
	"fmt"
	"math/big"
	"wlan/eapol"
	"wlan/wlan/elements"
)

type FourWayConfig struct {
	Transport eapol.Transport

	// PMK computation.
	PassPhrase string
	SSID       string

	// PTK computation.
	PeerAddr [6]uint8
	StaAddr  [6]uint8
	aNonce   [32]byte
	sNonce   [32]byte

	// Validation.
	BeaconRSNE *elements.RSN
	AssocRSNE  *elements.RSN
}

type FourWay struct {
	config           FourWayConfig
	keyReplayCounter [8]uint8
}

func NewFourWay(config FourWayConfig) *FourWay {
	return &FourWay{config: config}
}

func (hs *FourWay) HandleEAPOLKeyFrame(f *eapol.KeyFrame) error {
	if integrous, err := hs.isIntegrous(f); !integrous {
		return err
	}

	// TODO(hahnr): Verify whether the message fits and integrates into the current state of the
	// system. For example, we cannot receive the third message before the first one.

	// TODO(hahnr): Once the state verification succeeded, process the message's content.
	return nil
}

// Verifies a given message for integrity. A message passes the Integrity check successfully if it
// contains no incorrect information. An example of incorrect information is, the absence of a MIC
// even though the MIC bit is set, or if the first message of the handshake pretends to transmit a
// key.
// This method performs various checks based on the number of the message in the handshake, such as
// message 1, 2, or 3. These checks do not verify the state of the message or system, but instead,
// only the integrity of the message with respect to the expected information for the given message
// number in the handshake. For example, the third message of the handshake must have the MIC bit
// set while the first message must not.
func (hs *FourWay) isIntegrous(f *eapol.KeyFrame) (bool, error) {
	// Verify every received bit. Keep all checks as tight as possible.
	// This will likely block association with incorrectly implemented APs which don't strictly
	// follow specifications.

	// IEEE Std 802.11-2016 12.7.2 b.1.ii)
	// TODO(hahnr): Derive this value from the selected AKM.
	if f.Info.Extract(eapol.KeyInfo_DescriptorVersion) != 2 {
		return false, fmt.Errorf("unsupported DescriptorVersion for EAPOL Key frame")
	}

	// IEEE Std 802.11-2016 12.7.2 b.2)
	// TODO(hahnr): Add support for GTK exchange.
	if !f.Info.IsSet(eapol.KeyInfo_Type) {
		return false, fmt.Errorf("Group or SMK handshake is not supported")
	}

	// Use the message's number to check for message specific information being set. E.g., the Install
	// or MIC bit which both can only be set on the third message.
	isFirstMsg := !isThirdMessage(f)
	if isFirstMsg == f.Info.IsSet(eapol.KeyInfo_Encrypted_KeyData) {
		return false, fmt.Errorf("unexpected secure state")
	}

	// IEEE Std 802.11-2016 12.7.2 b.4)
	// Only the third message can request to install a key.
	if isFirstMsg == f.Info.IsSet(eapol.KeyInfo_Install) {
		return false, fmt.Errorf("unexpected Install bit")
	}

	// IEEE Std 802.11-2016 12.7.2 b.5)
	// Every message of the handshake sent from the Authenticator requires a response. Hence, ACK must
	// be set.
	if !f.Info.IsSet(eapol.KeyInfo_ACK) {
		return false, fmt.Errorf("expected response requirement")
	}

	// IEEE Std 802.11-2016 12.7.2 b.6)
	// Only the third message should have set a MIC.
	if isFirstMsg == f.Info.IsSet(eapol.KeyInfo_MIC) {
		return false, fmt.Errorf("unexpected MIC bit")
	}

	// IEEE Std 802.11-2016 12.7.2 b.8) & b.9)
	// Must never be set from authenticator.
	if f.Info.IsSet(eapol.KeyInfo_Error) || f.Info.IsSet(eapol.KeyInfo_Request) {
		return false, fmt.Errorf("Error and request bits cannot be set by Authenticator")
	}

	// IEEE Std 802.11-2016 12.7.2 b.10)
	// First message must *not* be encrypted, third one must.
	if isFirstMsg == f.Info.IsSet(eapol.KeyInfo_Encrypted_KeyData) {
		return false, fmt.Errorf("unexpected encryption state of data")
	}

	// IEEE Std 802.11-2016 12.7.2 b.11)
	if f.Info.IsSet(eapol.KeyInfo_SMK_Message) {
		return false, fmt.Errorf("SMK message cannot be set in non SMK handshake")
	}

	// IEEE Std 802.11-2016 12.7.2 c)
	// TODO(hahnr): Derive KeyLength from selected AKM.
	if f.Length != 16 {
		return false, fmt.Errorf("invalid KeyLength %u but expected %u", f.Length, 16)
	}

	// IEEE Std 802.11-2016, 12.7.2, d)
	// Replay counter must be larger than the one from the last valid received Key frame.
	// Only Key frames with a valid, non zero MIC increase the replay counter.
	minReplayCounter := new(big.Int).SetBytes(hs.keyReplayCounter[:])
	actualReplayCounter := new(big.Int).SetBytes(f.ReplayCounter[:])
	if minReplayCounter.Cmp(actualReplayCounter) != -1 {
		return false, fmt.Errorf("invalid KeyReplayCounter")
	}

	// IEEE Std 802.11-2016, 12.7.2, e)
	// None must be non zero in all received messages.
	if isZero(f.Nonce[:]) {
		return false, fmt.Errorf("invalid nonce, must not be zero")
	}
	// IEEE Std 80u2.11-2016, 12.7.2, f)
	// IV is not used. Skip check.

	// IEEE Std 802.11-2016, 12.7.2, g)
	// Must be zero in the first message and can be zero in the third.
	if isFirstMsg && !isZero(f.RSC[:]) {
		return false, fmt.Errorf("invalid rsc, must be zero")
	}

	// IEEE Std 802.11-2016, 12.7.2, h)
	// TODO(hahnr): Verify MIC if MIC bit was set. Drop all invalid frames.

	// IEEE Std 802.11-2016, 12.7.2, i) & j)
	// KeyDataLength can be zero in the first message and must not be zero in the third.
	// Note: If the first message contains data, it must be a PMKID. We should check this once PMKIDs
	// are supported.
	if !isFirstMsg && f.DataLength == 0 {
		return false, fmt.Errorf("expected data, received nothing")
	}

	return true, nil
}

// IEEE Std 802.11-2016 12.7.2 b.7)
// There is no general way of deriving the message's number from let's say a dedicated byte or so.
// As explained in the specification, the Secure bit must be set by the Authenticator in all Key
// frames which contain the last key needed to complete the Supplicant's initialization. In the
// 4-Way Handshake, this can only be the third message. Hence, we utilize this fact to determine the
// message number.
// Note: Since only the first and third message are sent by the Authenticator we can assume that
// every message which is not the third one must be the first one. The integrity check will filter
// out messages which we believe to be first messages but in fact are neither the first nor the
// third one.
func isThirdMessage(f *eapol.KeyFrame) bool {
	return f.Info.IsSet(eapol.KeyInfo_Secure)
}

func isZero(buf []byte) bool {
	for _, v := range buf {
		if v != 0 {
			return false
		}
	}
	return true
}

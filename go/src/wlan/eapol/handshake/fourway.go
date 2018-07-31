// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package handshake

import (
	"bytes"
	"encoding/hex"
	"fmt"
	"fidl/fuchsia/wlan/mlme"
	"log"
	"math/big"
	"wlan/eapol"
	"wlan/eapol/crypto"
	"wlan/keywrap"
	"wlan/wlan/elements"
)

// This is an implementation of the 4-Way Handshake used to exchange pair-wise and group keys.
// The Handshake only supports the role of a Supplicant so far.
// TODO(hahnr): Rename package to fourway.

const debug = true

type fourWayState interface {
	// Returns 'true' if the given Key frame can be handled by this state.
	isMessageAllowed(f *eapol.KeyFrame) bool
	handleEAPOLKeyFrame(hs *FourWay, f *eapol.KeyFrame) error
}

// Handshake not yet started. Waiting for the first message to be received.
type fourWayStateIdle struct{}

// Handshake started and successfully exchanged the first and second message.
// Waiting for the third message to be received.
// Note: If a valid first message is received the Handshake must be restarted.
type fourWayStateWaitingGTK struct{}

// The handshake successfully completed and all keys were exchanged and configured
// in the STA.
// TODO(hahnr): Define what the expected behavior is when the first message is
// received in this state.
type fourWayStateCompleted struct{}

type messageNumber uint8

const (
	Message1 messageNumber = iota + 1
	Message2
	Message3
	Message4
)

type FourWayConfig struct {
	Transport eapol.Transport

	// PMK computation.
	PassPhrase string
	SSID       string

	// PTK computation.
	PeerAddr [6]uint8
	_        [2]uint8 // make the symbol 4-byte aligned to avoid a compiler bug (https://github.com/golang/go/issues/19137)
	StaAddr  [6]uint8

	// Validation.
	BeaconRSNE *elements.RSN
	AssocRSNE  *elements.RSN
}

type FourWay struct {
	config           FourWayConfig
	keyReplayCounter [8]uint8
	aNonce           [32]byte
	sNonce           [32]byte
	ptk              *crypto.PTK
	gtk              []byte
	gtkID            uint8
	state            fourWayState
}

func NewFourWay(config FourWayConfig) *FourWay {
	return &FourWay{
		config: config,
		state:  &fourWayStateIdle{},
	}
}

func (hs *FourWay) HandleEAPOLKeyFrame(s *Supplicant, f *eapol.KeyFrame) error {
	// Validate frame's integrity.
	if integrous, err := hs.isIntegrous(f); !integrous {
		return err
	}

	// Validate frame fits into the current state of the system.
	if compliant, err := hs.isStateCompliant(f); !compliant {
		return err
	}

	// Frames which made it this far are valid and must be processed.
	// Note: Frames with an encrypted data field will be encrypted by the time of reaching this point.
	// However, their data was not checked against any constraints or requirements, mostly because the
	// data is often context specific and cannot be validated at such an early process.
	return hs.state.handleEAPOLKeyFrame(hs, f)
}

func (hs *FourWay) IsComplete() bool {
	switch hs.state.(type) {
	case *fourWayStateCompleted:
		return true
	default:
		return false
	}
}

func (s *fourWayStateIdle) isMessageAllowed(f *eapol.KeyFrame) bool {
	return getMessageNumber(f) == Message1
}

// First state of the Handshake. Preceding state and integrity checks guarantee that only the first
// message of the Handshake will be processed by this state.
func (s *fourWayStateIdle) handleEAPOLKeyFrame(hs *FourWay, msg *eapol.KeyFrame) error {
	if err := s.handleMessage1(hs, msg); err != nil {
		return err
	}

	if err := s.sendMessage2(hs, msg); err != nil {
		return err
	}

	hs.state = &fourWayStateWaitingGTK{}
	return nil
}

// IEEE Std 802.11-2016, 12.7.6.2
// With the first message of the Handshake the Supplicant is able to derive the PTK.
func (s *fourWayStateIdle) handleMessage1(hs *FourWay, msg1 *eapol.KeyFrame) error {
	pmk, err := crypto.PSK(hs.config.PassPhrase, hs.config.SSID)
	if err != nil {
		return err
	}

	hs.aNonce = msg1.Nonce
	hs.sNonce = crypto.GetNonce(hs.config.StaAddr)
	hs.ptk = crypto.DeriveKeys(pmk, hs.config.StaAddr[:], hs.config.PeerAddr[:], hs.aNonce[:], hs.sNonce[:])
	if debug {
		log.Println("PMK: ", hex.EncodeToString(pmk))
		log.Println("TK: ", hex.EncodeToString(hs.ptk.TK))
		log.Println("KCK: ", hex.EncodeToString(hs.ptk.KCK))
		log.Println("KEK: ", hex.EncodeToString(hs.ptk.KEK))
	}
	return nil
}

// IEEE Std 802.11-2016, 12.7.6.3
// The Supplicant sends the generated sNonce in the second message such that the Authenticator can
// derive the PTK as well.
func (s *fourWayStateIdle) sendMessage2(hs *FourWay, msg1 *eapol.KeyFrame) error {
	info := msg1.Info.Update(eapol.KeyInfo_ACK|eapol.KeyInfo_Install|eapol.KeyInfo_Encrypted_KeyData, eapol.KeyInfo_MIC)
	data := hs.config.AssocRSNE.Bytes()
	message2 := &eapol.KeyFrame{
		Header: eapol.Header{
			Version:    msg1.Header.Version,
			PacketType: eapol.PacketType_Key,
		},
		DescriptorType: msg1.DescriptorType,
		Info:           info,
		ReplayCounter:  msg1.ReplayCounter,
		Nonce:          hs.sNonce,
		DataLength:     uint16(len(data)),
		Data:           data,
		MIC:            make([]byte, 16), // TODO(hahnr): MIC size must be derived from selected AKM.
	}
	message2.UpdateMIC(hs.ptk.KCK)
	return hs.config.Transport.SendEAPOLRequest(hs.config.StaAddr, hs.config.PeerAddr, message2)
}

func (s *fourWayStateWaitingGTK) isMessageAllowed(f *eapol.KeyFrame) bool {
	messageNumber := getMessageNumber(f)
	return messageNumber == Message1 || messageNumber == Message3
}

// Second state of the Handshake can receive either the first or third message of the Handshake.
// When the first message is received, the Handshake will be restarted. If the third one is received
// the Handshake continues.
func (s *fourWayStateWaitingGTK) handleEAPOLKeyFrame(hs *FourWay, msg *eapol.KeyFrame) error {
	// If first message was received, restart the handshake.
	if getMessageNumber(msg) == Message1 {
		hs.ptk = nil
		hs.aNonce = [32]byte{}
		hs.state = &fourWayStateIdle{}
		return hs.state.handleEAPOLKeyFrame(hs, msg)
	}

	// Else we received the third message. Continue as usual.
	if err := s.handleMessage3(hs, msg); err != nil {
		return err
	}

	if err := s.sendMessage4(hs, msg); err != nil {
		return err
	}

	if err := s.configureKeysInStation(hs); err != nil {
		return err
	}

	hs.state = &fourWayStateCompleted{}
	return nil
}

// IEEE Std 802.11-2016, 12.7.6.4
// The third message of the Handshake contains a GTK and the associated RSNE. It might contain an
// additional, optional RSNE which overwrites the cipher choice.
func (s *fourWayStateWaitingGTK) handleMessage3(hs *FourWay, msg3 *eapol.KeyFrame) error {
	// IEEE Std 802.11-2016, 12.7.2 d)
	// The specification requires that if a valid MIC is set, the key replay counter must be
	// increased. Previous verifications guarantee that
	// (1) if a MIC is set, it is valid,
	// (2) if the handshake's third message was received, it has a MIC set.
	hs.keyReplayCounter = msg3.ReplayCounter

	// Extract GTK and RSNE from message's data.
	gtkKDE, rsne, err := ExtractInfoFromMessage3(msg3.Data)
	if err != nil {
		return err
	}
	if rsne == nil {
		return fmt.Errorf("Message 3 does not hold an RSNE")
	}
	if gtkKDE == nil {
		return fmt.Errorf("Message 3 does not hold a GTK")
	}
	if bytes.Compare(rsne.Raw, hs.config.BeaconRSNE.Bytes()) != 0 {
		return fmt.Errorf("Message 3 has a different RSNE from the Beacon one")
	}
	if debug {
		log.Println("GTK: ", hex.EncodeToString(gtkKDE.GTK))
		log.Println("GTK Index: ", gtkKDE.KeyID)
	}
	hs.gtk = gtkKDE.GTK
	hs.gtkID = gtkKDE.KeyID
	// TODO(hahnr): Verify GTK length meets expected length (CCMP = 16B, TKIP = 32B).
	return nil
}

// IEEE Std 802.11-2016, 12.7.6.5
// The last message of the Handshake lets the Authenticator know that the GTK was received and the
// Handshake completed successfully. Once the Handshake completes, the traffic should be protected
// with the corresponding keys.
func (s *fourWayStateWaitingGTK) sendMessage4(hs *FourWay, msg3 *eapol.KeyFrame) error {
	info := msg3.Info.Update(eapol.KeyInfo_Install|eapol.KeyInfo_ACK|eapol.KeyInfo_Error|eapol.KeyInfo_Request|eapol.KeyInfo_Encrypted_KeyData, eapol.KeyInfo_MIC|eapol.KeyInfo_Secure)
	message4 := &eapol.KeyFrame{
		Header: eapol.Header{
			Version:    msg3.Header.Version,
			PacketType: eapol.PacketType_Key,
		},
		DescriptorType: msg3.DescriptorType,
		Info:           info,
		ReplayCounter:  msg3.ReplayCounter,
		MIC:            make([]byte, 16), // TODO(hahnr): MIC size must be derived from selected AKM.
	}
	message4.UpdateMIC(hs.ptk.KCK)
	return hs.config.Transport.SendEAPOLRequest(hs.config.StaAddr, hs.config.PeerAddr, message4)
}

func (s *fourWayStateWaitingGTK) configureKeysInStation(hs *FourWay) error {
	keyList := []mlme.SetKeyDescriptor{}

	if hs.config.AssocRSNE.GroupData != nil {
		cipher := hs.config.AssocRSNE.GroupData
		keyList = append(keyList, mlme.SetKeyDescriptor{
			Key:             hs.gtk,
			KeyType:         mlme.KeyType(mlme.KeyTypeGroup),
			KeyId:           uint16(hs.gtkID),
			CipherSuiteOui:  cipher.OUI,
			CipherSuiteType: uint8(cipher.Type),
		})
	}

	if len(hs.config.AssocRSNE.PairwiseCiphers) > 0 {
		// The STA selects the pairwise cipher by comparing all its supported cipher suites to the ones
		// announced by the AP's Beacon frames. One cipher suite of the intersecting set is chosen and the
		// AP is informed about the selected cipher suite in the Association request sent from the STA.
		// Even though it is technically possible, it makes no sense for a STA to send a list with more
		// than one pairwise cipher suites to the AP in its Association request. In this case, it would be
		// unclear which suite to use. Hence, the simple assumption that the first pairwise cipher suite
		// of a none empty list is the negotiated suite.
		cipher := hs.config.AssocRSNE.PairwiseCiphers[0]
		keyList = append(keyList, mlme.SetKeyDescriptor{
			Key:             hs.ptk.TK,
			KeyType:         mlme.KeyType(mlme.KeyTypePairwise),
			Address:         hs.config.PeerAddr,
			CipherSuiteOui:  cipher.OUI,
			CipherSuiteType: uint8(cipher.Type),
		})
	}

	return hs.config.Transport.SendSetKeysRequest(keyList)
}

func (s *fourWayStateCompleted) isMessageAllowed(f *eapol.KeyFrame) bool {
	// TODO(hahnr): Implement.
	return true
}

func (s *fourWayStateCompleted) handleEAPOLKeyFrame(hs *FourWay, f *eapol.KeyFrame) error {
	// TODO(hahnr): Implement re-keying for PTK and GTK.
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

	// IEEE Std 802.1X-2010, 11.9
	// Use of RC4 is deprecated.
	if f.DescriptorType != eapol.KeyDescriptorType_IEEE_802_11 {
		return false, fmt.Errorf("unsupported Key Descriptor Type %d, expected %d", f.DescriptorType, eapol.KeyDescriptorType_IEEE_802_11)
	}

	// IEEE Std 802.11-2016, 12.7.2 b.1.ii)
	// TODO(hahnr): Derive this value from the selected AKM.
	if f.Info.Extract(eapol.KeyInfo_DescriptorVersion) != 2 {
		return false, fmt.Errorf("unsupported DescriptorVersion for EAPOL Key frame")
	}

	// IEEE Std 802.11-2016, 12.7.2 b.2)
	// TODO(hahnr): Add support for GTK exchange.
	if !f.Info.IsSet(eapol.KeyInfo_Type) {
		return false, fmt.Errorf("Group or SMK handshake is not supported")
	}

	// Use the message's number to check for message specific information being set. E.g., the Install
	// or MIC bit which both can only be set on the third message.
	isFirstMsg := getMessageNumber(f) == Message1
	if isFirstMsg == f.Info.IsSet(eapol.KeyInfo_Encrypted_KeyData) {
		return false, fmt.Errorf("unexpected secure state")
	}

	// IEEE Std 802.11-2016, 12.7.2 b.4)
	// Only the third message can request to install a key.
	if isFirstMsg == f.Info.IsSet(eapol.KeyInfo_Install) {
		return false, fmt.Errorf("unexpected Install bit")
	}

	// IEEE Std 802.11-2016, 12.7.2 b.5)
	// Every message of the handshake sent from the Authenticator requires a response. Hence, ACK must
	// be set.
	if !f.Info.IsSet(eapol.KeyInfo_ACK) {
		return false, fmt.Errorf("expected response requirement")
	}

	// IEEE Std 802.11-2016, 12.7.2 b.6)
	// Only the third message should have set a MIC.
	if isFirstMsg == f.Info.IsSet(eapol.KeyInfo_MIC) {
		return false, fmt.Errorf("unexpected MIC bit")
	}
	// IEEE Std 802.11-2016, 12.7.6.2
	if isFirstMsg && !isZero(f.MIC[:]) {
		// Some routers don't zero the MIC in the first message of the handshake. Rather than refusing the
		// handshake entirely, fix the glitch ourselves (NET-927).
		f.MIC = make([]byte, len(f.MIC))
	}

	// IEEE Std 802.11-2016, 12.7.2 b.8) & b.9)
	// Must never be set from authenticator.
	if f.Info.IsSet(eapol.KeyInfo_Error) || f.Info.IsSet(eapol.KeyInfo_Request) {
		return false, fmt.Errorf("Error and request bits cannot be set by Authenticator")
	}

	// IEEE Std 802.11-2016, 12.7.2 b.10)
	// First message must *not* be encrypted, third one must.
	if isFirstMsg == f.Info.IsSet(eapol.KeyInfo_Encrypted_KeyData) {
		return false, fmt.Errorf("unexpected encryption state of data")
	}

	// IEEE Std 802.11-2016, 12.7.2 b.11)
	if f.Info.IsSet(eapol.KeyInfo_SMK_Message) {
		return false, fmt.Errorf("SMK message cannot be set in non SMK handshake")
	}

	// IEEE Std 802.11-2016, 12.7.2 c)
	// TODO(hahnr): Derive KeyLength from selected AKM.
	if f.Length != 16 {
		return false, fmt.Errorf("invalid KeyLength %d but expected %d", f.Length, 16)
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
	// IEEE Std 802.11-2016, 12.7.6.2
	if isFirstMsg && !isZero(f.IV[:]) {
		return false, fmt.Errorf("invalid IV, must be zero")
	}

	// IEEE Std 802.11-2016, 12.7.2, i) & j)
	// KeyDataLength can be zero in the first message and must not be zero in the third.
	// Note: If the first message contains data, it must be a PMKID. We should check this once PMKIDs
	// are supported.
	// The declared KeyDataLength must match the length of the Data field.
	if !isFirstMsg && f.DataLength == 0 {
		return false, fmt.Errorf("expected data, received nothing")
	}
	if f.DataLength != uint16(len(f.Data)) {
		return false, fmt.Errorf("expected DataLength %d to match length of Data %d", f.DataLength, len(f.Data))
	}

	return true, nil
}

// Verifies whether the Key frame is expected and aligns with the system's current state. E.g., a
// frame which's MIC cannot be verified due to a missing KCK or wrong MIC will be refused.
// Note: This method will decrypt the key's data and write the decrypted data buffer to the frame's
// data field.
func (hs *FourWay) isStateCompliant(f *eapol.KeyFrame) (bool, error) {
	messageNumber := getMessageNumber(f)
	if !hs.state.isMessageAllowed(f) {
		return false, fmt.Errorf("the current state \"%v\" did not expect message %d", hs.state, messageNumber)
	}

	// IEEE Std 802.11-2016, 12.7.2, d)
	// Replay counter must be larger than the one from the last valid received Key frame.
	// Only Key frames with a valid, non zero MIC increase the replay counter.
	minReplayCounter := new(big.Int).SetBytes(hs.keyReplayCounter[:])
	actualReplayCounter := new(big.Int).SetBytes(f.ReplayCounter[:])
	if minReplayCounter.Cmp(actualReplayCounter) != -1 {
		return false, fmt.Errorf("invalid KeyReplayCounter")
	}

	// If a MIC is set but there is no PTK yet, the MIC cannot be verified.
	if f.Info.IsSet(eapol.KeyInfo_MIC) && hs.ptk == nil {
		return false, fmt.Errorf("cannot validate MIC")
	}

	// IEEE Std 802.11-2016, 12.7.2, h)
	// Verify the message's MIC.
	if f.Info.IsSet(eapol.KeyInfo_MIC) && !f.HasValidMIC(hs.ptk.KCK) {
		return false, fmt.Errorf("invalid MIC %s", hex.EncodeToString(f.MIC))
	}

	// If the message's data is encrypted but there is no PTK yet, the data cannot be decrypted.
	if f.Info.IsSet(eapol.KeyInfo_Encrypted_KeyData) && hs.ptk == nil {
		return false, fmt.Errorf("cannot unwrap key data")
	}

	// Decrypt data and verify it succeeded.
	if f.Info.IsSet(eapol.KeyInfo_Encrypted_KeyData) {
		data, err := keywrap.Unwrap(hs.ptk.KEK, f.Data)
		if err != nil {
			return false, fmt.Errorf("error decrypting frame data")
		}
		f.Data = data
	}

	// Third message must hold the same nonce as the first message.
	if messageNumber == Message3 && bytes.Compare(f.Nonce[:], hs.aNonce[:]) != 0 {
		return false, fmt.Errorf("invalid nonce received")
	}

	// TODO(hahnr): Validate RSNE sent in third message with association RSNE.
	// Requires KDE & Element reader.

	return true, nil
}

// Extracts GTK KDE and first RSNE from a given key data. Either can be nil if it was not specified.
// Note: A second, optional RSNE is ignored so far.
func ExtractInfoFromMessage3(keyData []byte) (*eapol.GTKKDE, *eapol.RSNElement, error) {
	var gtkKDE *eapol.GTKKDE
	var rsne *eapol.RSNElement

	reader := eapol.NewKeyDataReader(keyData)
	for reader.CanRead() {
		switch reader.PeekType() {
		case eapol.KeyDataItemType_KDE:
			hdr := reader.PeekKDE()

			// Skip unknown KDEs.
			knownOUI := bytes.Compare(eapol.KDE_OUI[:], hdr.OUI[:]) == 0
			if !knownOUI || hdr.DataType != eapol.KDEType_GTK {
				// Read unknown KDE to progress reader.
				reader.ReadKDE(nil)
				break
			}

			gtkKDE = &eapol.GTKKDE{}
			if err := reader.ReadKDE(gtkKDE); err != nil {
				return nil, nil, err
			}
		case eapol.KeyDataItemType_Element:
			elemHdr := reader.PeekElement()

			// Skip unknown elements and an optional, second RSNE which the AP
			// provided to change the selected ciphers.
			// TODO(hahnr): Work with second optional cipher if one was specified.
			if elemHdr.Id != eapol.ElementID_Rsn || rsne != nil {
				// Read the element to progress the reader.
				reader.ReadElement(nil)
				break
			}

			rsne = &eapol.RSNElement{}
			if err := reader.ReadElement(rsne); err != nil {
				return nil, nil, err
			}
		default:
			return nil, nil, fmt.Errorf("error while processing KeyData")
		}
	}

	return gtkKDE, rsne, nil
}

func getMessageNumber(f *eapol.KeyFrame) messageNumber {
	// There is no general way of deriving the message's number from let's say a dedicated byte but
	// instead the message's number must be derived from the message's structure and content.
	// The Handshake so far only supports the role of the Supplicant. Hence, only third or first
	// messages are expected to be received. This needs to be adjusted once also an Authenticator role
	// is supported.

	// IEEE Std 802.11-2016 12.7.2 b.7)
	// As explained in the specification, the Secure bit must be set by the Authenticator in all Key
	// frames which contain the last key needed to complete the Supplicant's initialization. In the
	// 4-Way Handshake, this can only be the third message. Hence, we utilize this fact to determine the
	// message number.
	// Note: Since only the first and third message are sent by the Authenticator we can assume that
	// every message which is not the third one must be the first one. The integrity check will filter
	// out messages which we believe to be first messages but in fact are neither the first nor the
	// third one.
	if f.Info.IsSet(eapol.KeyInfo_Secure) {
		return Message3
	}
	return Message1
}

func isZero(buf []byte) bool {
	for _, v := range buf {
		if v != 0 {
			return false
		}
	}
	return true
}

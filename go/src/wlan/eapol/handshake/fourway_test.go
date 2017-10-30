// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package handshake_test

import (
	"bytes"
	"encoding/hex"
	"errors"
	mlme "garnet/public/lib/wlan/fidl/wlan_mlme"
	"testing"
	"wlan/eapol"
	"wlan/eapol/crypto"
	. "wlan/eapol/handshake"
	"wlan/wlan/elements"
)

// Tests the Supplicant role of the 4-Way Handshake.

var ErrMessage2NotSent = errors.New("Couldn't sent message 2")

type inputTable struct {
	BSSID          [6]uint8
	STAAddr        [6]uint8
	ANonce         [32]uint8
	SSID           string
	PassPhrase     string
	BeaconRSNEData string
	AssocRSNEData  string
}

var inputTables = []*inputTable{
	{
		BSSID:          [6]byte{0x1, 0x2, 0x3, 0x4, 0x5, 0x6},
		STAAddr:        [6]byte{0xA, 0xB, 0xC, 0xD, 0xE, 0xF},
		ANonce:         [32]byte{0xEF},
		SSID:           "Test SSID",
		PassPhrase:     "test12345678",
		BeaconRSNEData: "30360100000fac0c0300000fac0c01020304000fac010200010203020102030d39050100000102030405060708090a0b0c0d0e0f000fac02",
		AssocRSNEData:  "30140100010203040100000fac010100010203020000",
	},
	{
		BSSID:          [6]byte{0x3, 0x2, 0xAA, 0xF, 0xEE, 0x6},
		STAAddr:        [6]byte{0xCC, 0xBB, 0x11, 0x22, 0x33, 0x55},
		ANonce:         [32]byte{0x99, 0x55},
		SSID:           "Unicode SSID \u2318\u2318",
		PassPhrase:     "oi#(H 892h3t2893#*",
		BeaconRSNEData: "30360100000fac0c0300000fac0c01020304000fac010200010203020102030d39050100000102030405060708090a0b0c0d0e0f000fac02",
		AssocRSNEData:  "30140100010203040100000fac010100010203020000",
	},
}

// TransportMock is an EAPOL.Transport implementation to intercept and mock response transport from
// the Supplicant or Authenticator (which ever role is tested).
type transportMock struct {
	// Capture SendEAPOLRequest(...)
	lastSrcAddr       [6]uint8
	lastDstAddr       [6]uint8
	lastEapolKeyFrame *eapol.KeyFrame

	// Capture SendSetKeysRequest(...)
	lastKeyList []mlme.SetKeyDescriptor

	// Error returned upon the next request.
	nextRequestError error
}

// Each test runs in an environment which provides convenience methods and access to information
// such as the handshake configuration and current input table.
type env struct {
	transport transportMock
	fourway   *FourWay
	testEnv   *testing.T
	config    *FourWayConfig
	input     *inputTable
}

// Send valid message 1 to the Supplicant and verify the response is IEEE compliant.
func TestSupplicant_ResponseToValidMessage1(t *testing.T) {
	for _, table := range inputTables {
		env := NewEnv(t, table)

		// Send message 1 to Supplicant.
		msg1 := env.CreateValidMessage1()
		err := env.SendToTestSubject(msg1)
		if err != nil {
			t.Fatal("Unexpected error:", err)
		}
		msg2 := env.RetrieveTestSubjectResponse()
		env.ExpectValidMessage2(msg1, msg2)
	}
}

// Send two valid message 1 to the Supplicant. Supplicant's responses should be IEEE compliant and
// the sNonce should not be reused.
func TestSupplicant_ResponseToValidReplayedMessage1(t *testing.T) {
	for _, table := range inputTables {
		env := NewEnv(t, table)

		// Send message 1 for first time.
		msg1 := env.CreateValidMessage1()
		err := env.SendToTestSubject(msg1)
		if err != nil {
			t.Fatal("Unexpected error:", err)
		}
		msg2_1 := env.RetrieveTestSubjectResponse()
		env.ExpectValidMessage2(msg1, msg2_1)

		// Send a second message 1.
		msg1 = env.CreateValidMessage1()
		err = env.SendToTestSubject(msg1)
		if err != nil {
			t.Fatal("Unexpected error:", err)
		}
		msg2_2 := env.RetrieveTestSubjectResponse()
		env.ExpectValidMessage2(msg1, msg2_2)

		// Ensure sNonce was not reused.
		if bytes.Compare(msg2_1.Nonce[:], msg2_2.Nonce[:]) == 0 {
			t.Fatal("sNonce was reused")
		}
	}
}

// Send a valid message 1 to the Supplicant followed by an invalid message 1.
// Supplicant should respond to the first message with a IEEE compliant message 2 but should drop
// the second message 1.
func TestSupplicant_ResponseToInvalidReplayedMessage1(t *testing.T) {
	for _, table := range inputTables {
		env := NewEnv(t, table)

		// Send message 1 for first time.
		msg1 := env.CreateValidMessage1()
		err := env.SendToTestSubject(msg1)
		if err != nil {
			t.Fatal("Unexpected error:", err)
		}
		msg2_1 := env.RetrieveTestSubjectResponse()
		env.ExpectValidMessage2(msg1, msg2_1)

		// Send an invalid second message 1.
		msg1 = env.CreateValidMessage1()
		msg1.Info = msg1.Info.Update(0, eapol.KeyInfo_MIC)
		err = env.SendToTestSubject(msg1)
		if err == nil {
			t.Fatal("Expected error with invalid message 1")
		}
		msg2_2 := env.RetrieveTestSubjectResponse()
		if msg2_2 != nil {
			t.Fatal("Expected Supplicant to not respond to invalid message 1")
		}
	}
}

// Send a valid message 1 to the Supplicant and pretend that the EAPOL Transport layer fails to
// transmit the Supplicant's message 2 response.
func TestSupplicant_FailMessage2Transport(t *testing.T) {
	for _, table := range inputTables {
		env := NewEnv(t, table)

		// Setup environment to fail the next EAPOL transport.
		env.transport.nextRequestError = ErrMessage2NotSent

		// Send message 1 to Supplicant.
		msg1 := env.CreateValidMessage1()
		err := env.SendToTestSubject(msg1)
		if err != ErrMessage2NotSent {
			t.Fatal("Expected transport error when sending message 2 but error was:", err)
		}
		// Verify the Supplicant's response would have been a valid one.
		msg2 := env.RetrieveTestSubjectResponse()
		env.ExpectValidMessage2(msg1, msg2)
	}
}

// Send all forms of invalid message 1 to the Supplicant. The Supplicant must drop every message and
// never respond to any.
// See IEEE Std 802.11-2016, 12.7.6.2 for what makes a valid message 1.
func TestSupplicant_DropInvalidMessage1(t *testing.T) {
	for _, table := range inputTables {
		env := NewEnv(t, table)
		var f *eapol.KeyFrame

		// Invalid Key Descriptor: ARC4 with HMAC-MD5.
		f = env.CreateValidMessage1()
		f.Info = f.Info.Update(eapol.KeyInfo_DescriptorVersion, 0)
		f.Info = f.Info.Update(eapol.KeyInfo_Type, eapol.KeyInfo_DescriptorVersion&1)
		err := env.SendToTestSubject(f)
		resp := env.RetrieveTestSubjectResponse()
		if err == nil || resp != nil {
			t.Fatal("Expected error with invalid key descriptor")
		}

		// Invalid Key Descriptor: AES with AES-128-CMCAC.
		f = env.CreateValidMessage1()
		f.Info = f.Info.Update(eapol.KeyInfo_DescriptorVersion, 0)
		f.Info = f.Info.Update(eapol.KeyInfo_Type, eapol.KeyInfo_DescriptorVersion&3)
		err = env.SendToTestSubject(f)
		resp = env.RetrieveTestSubjectResponse()
		if err == nil || resp != nil {
			t.Fatal("Expected error with invalid key descriptor")
		}

		// Invalid Key Descriptor: None.
		f = env.CreateValidMessage1()
		f.Info = f.Info.Update(eapol.KeyInfo_DescriptorVersion|eapol.KeyInfo_Type, 0)
		err = env.SendToTestSubject(f)
		resp = env.RetrieveTestSubjectResponse()
		if err == nil || resp != nil {
			t.Fatal("Expected error with invalid key descriptor")
		}

		// Invalid Key Type.
		f = env.CreateValidMessage1()
		f.Info = f.Info.Update(eapol.KeyInfo_Type, 0)
		err = env.SendToTestSubject(f)
		resp = env.RetrieveTestSubjectResponse()
		if err == nil || resp != nil {
			t.Fatal("Expected error with invalid key type")
		}

		// SMK Message bit set.
		f = env.CreateValidMessage1()
		f.Info = f.Info.Update(0, eapol.KeyInfo_SMK_Message)
		err = env.SendToTestSubject(f)
		resp = env.RetrieveTestSubjectResponse()
		if err == nil || resp != nil {
			t.Fatal("Expected error with SMK Message bit set")
		}

		// MIC bit set.
		f = env.CreateValidMessage1()
		f.Info = f.Info.Update(0, eapol.KeyInfo_MIC)
		err = env.SendToTestSubject(f)
		resp = env.RetrieveTestSubjectResponse()
		if err == nil || resp != nil {
			t.Fatal("Expected error with MIC bit set")
		}

		// MIC bit and MIC set.
		f = env.CreateValidMessage1()
		f.Info = f.Info.Update(0, eapol.KeyInfo_MIC)
		copy(f.MIC, []uint8{3}) // Note: We cannot compute a valid MIC since we have no key
		err = env.SendToTestSubject(f)
		resp = env.RetrieveTestSubjectResponse()
		if err == nil || resp != nil {
			t.Fatal("Expected error with MIC set")
		}

		// MIC set.
		f = env.CreateValidMessage1()
		copy(f.MIC, []uint8{3}) // Note: We cannot compute a valid MIC since we have no key
		err = env.SendToTestSubject(f)
		resp = env.RetrieveTestSubjectResponse()
		if err == nil || resp != nil {
			t.Fatal("Expected error with MIC set")
		}

		// Secure bit set.
		f = env.CreateValidMessage1()
		f.Info = f.Info.Update(0, eapol.KeyInfo_Secure)
		err = env.SendToTestSubject(f)
		resp = env.RetrieveTestSubjectResponse()
		if err == nil || resp != nil {
			t.Fatal("Expected error with Secure bit set")
		}

		// Error bit set.
		f = env.CreateValidMessage1()
		f.Info = f.Info.Update(0, eapol.KeyInfo_Error)
		err = env.SendToTestSubject(f)
		resp = env.RetrieveTestSubjectResponse()
		if err == nil || resp != nil {
			t.Fatal("Expected error with Error bit set")
		}

		// Request bit set.
		f = env.CreateValidMessage1()
		f.Info = f.Info.Update(0, eapol.KeyInfo_Request)
		err = env.SendToTestSubject(f)
		resp = env.RetrieveTestSubjectResponse()
		if err == nil || resp != nil {
			t.Fatal("Expected error with Request bit set")
		}

		// Encrypted Data bit set.
		f = env.CreateValidMessage1()
		f.Info = f.Info.Update(0, eapol.KeyInfo_Encrypted_KeyData)
		err = env.SendToTestSubject(f)
		resp = env.RetrieveTestSubjectResponse()
		if err == nil || resp != nil {
			t.Fatal("Expected error with Encrypted Data bit set")
		}

		// Unsupported Key Length set.
		f = env.CreateValidMessage1()
		f.Length = 32
		err = env.SendToTestSubject(f)
		resp = env.RetrieveTestSubjectResponse()
		if err == nil || resp != nil {
			t.Fatal("Expected error with unsupported key length set")
		}

		// Unsupported Key Length set.
		f = env.CreateValidMessage1()
		f.Length = 32
		err = env.SendToTestSubject(f)
		resp = env.RetrieveTestSubjectResponse()
		if err == nil || resp != nil {
			t.Fatal("Expected error with unsupported key length set")
		}

		// All zero nonce.
		f = env.CreateValidMessage1()
		f.Nonce = [32]byte{}
		err = env.SendToTestSubject(f)
		resp = env.RetrieveTestSubjectResponse()
		if err == nil || resp != nil {
			t.Fatal("Expected error with all zero nonce")
		}

		// None zero IV.
		f = env.CreateValidMessage1()
		f.IV[0] = 1
		err = env.SendToTestSubject(f)
		resp = env.RetrieveTestSubjectResponse()
		if err == nil || resp != nil {
			t.Fatal("Expected error with none zero IV set")
		}

		// None zero RSC.
		f = env.CreateValidMessage1()
		f.RSC[0] = 1
		err = env.SendToTestSubject(f)
		resp = env.RetrieveTestSubjectResponse()
		if err == nil || resp != nil {
			t.Fatal("Expected error with none zero RSC set")
		}

		// Invalid Data Length.
		// The first message can contain data, however, DataLength and length of Data must match.
		f = env.CreateValidMessage1()
		f.Data = make([]byte, 20)
		f.DataLength = 16
		err = env.SendToTestSubject(f)
		resp = env.RetrieveTestSubjectResponse()
		if err == nil || resp != nil {
			t.Fatal("Expected error with invalid Data length")
		}
	}
}

// TODO(hahnr): Add tests for:
// (1) Send valid message 3 in first state.
// (2) Send valid message 3 in second state.
// (3) Send invalid message 3 in second state.
// (4) Replay message 3 in second state.
// (5) Replay message 1 in second state.
// (6) Verify Keys are configured in MLME.

func NewEnv(t *testing.T, input *inputTable) *env {
	e := &env{
		transport: transportMock{},
		testEnv:   t,
		input:     input,
	}
	config := FourWayConfig{
		AssocRSNE:  GetRSNE(input.AssocRSNEData),
		BeaconRSNE: GetRSNE(input.BeaconRSNEData),
		PeerAddr:   input.BSSID,
		StaAddr:    input.STAAddr,
		Transport:  &e.transport,
		SSID:       input.SSID,
		PassPhrase: input.PassPhrase,
	}
	e.config = &config
	e.fourway = NewFourWay(config)
	return e
}

// Env implementation.

// See IEEE Std 802.11-2016, 12.7.6.3 for what makes a valid message 2.
func (env *env) ExpectValidMessage2(msg1 *eapol.KeyFrame, msg2 *eapol.KeyFrame) {
	if msg2 == nil {
		env.testEnv.Fatal("Message 2 is nil")
	}

	if msg2.DescriptorType != msg1.DescriptorType {
		env.testEnv.Fatal("Message 2 contains invalid Descriptor Type")
	}

	if (msg2.Info & eapol.KeyInfo_DescriptorVersion) != (msg1.Info & eapol.KeyInfo_DescriptorVersion) {
		env.testEnv.Fatal("Message 2 contains invalid Descriptor Version")
	}

	if (msg2.Info & eapol.KeyInfo_Type) != (msg1.Info & eapol.KeyInfo_Type) {
		env.testEnv.Fatal("Message 2 contains invalid Key Type bit")
	}

	if (msg2.Info & eapol.KeyInfo_SMK_Message) != (msg1.Info & eapol.KeyInfo_SMK_Message) {
		env.testEnv.Fatal("Message 2 contains invalid SMK message bit")
	}

	if msg2.Info.IsSet(eapol.KeyInfo_Install) {
		env.testEnv.Fatal("Message 2 contains invalid Install bit")
	}

	if msg2.Info.IsSet(eapol.KeyInfo_ACK) {
		env.testEnv.Fatal("Message 2 contains invalid ACK bit")
	}

	if !msg2.Info.IsSet(eapol.KeyInfo_MIC) {
		env.testEnv.Fatal("Message 2 contains invalid MIC bit")
	}

	if (msg2.Info & eapol.KeyInfo_Secure) != (msg1.Info & eapol.KeyInfo_Secure) {
		env.testEnv.Fatal("Message 2 contains invalid Secure bit")
	}

	if (msg2.Info & eapol.KeyInfo_Error) != (msg1.Info & eapol.KeyInfo_Error) {
		env.testEnv.Fatal("Message 2 contains invalid Error bit")
	}

	if (msg2.Info & eapol.KeyInfo_Request) != (msg1.Info & eapol.KeyInfo_Request) {
		env.testEnv.Fatal("Message 2 contains invalid Request bit")
	}

	if msg2.Info.IsSet(eapol.KeyInfo_Encrypted_KeyData) {
		env.testEnv.Fatal("Message 2 contains invalid Encrypted Key Data bit")
	}

	if msg2.Length != 0 {
		env.testEnv.Fatal("Message 2 contains invalid Key Length")
	}

	if bytes.Compare(msg2.ReplayCounter[:], msg1.ReplayCounter[:]) != 0 {
		env.testEnv.Fatal("Message 2 contains invalid Key Replay Counter")
	}

	if bytes.Compare(msg2.Nonce[:], make([]byte, 32)) == 0 {
		env.testEnv.Fatal("Message 2 contains invalid None")
	}

	if bytes.Compare(msg2.IV[:], make([]byte, 16)) != 0 {
		env.testEnv.Fatal("Message 2 contains invalid IV")
	}

	if bytes.Compare(msg2.RSC[:], make([]byte, 8)) != 0 {
		env.testEnv.Fatal("Message 2 contains invalid RSC")
	}

	ptk := env.getPTK(msg2.Nonce)
	if !msg2.HasValidMIC(ptk.KCK) {
		env.testEnv.Fatal("Message 2 contains invalid MIC")
	}

	if msg2.DataLength != uint16(len(msg2.Data)) {
		env.testEnv.Fatal("Message 2 contains invalid Data Length")
	}

	if bytes.Compare(msg2.Data, env.config.AssocRSNE.Bytes()) != 0 {
		env.testEnv.Fatal("Message 2 contains invalid Data")
	}
}

func (env *env) CreateValidMessage1() *eapol.KeyFrame {
	f := eapol.NewEmptyKeyFrame(128)
	f.DescriptorType = 2
	f.Info = (eapol.KeyInfo_DescriptorVersion & 2) | eapol.KeyInfo_Type | eapol.KeyInfo_ACK
	f.Length = 16
	f.ReplayCounter = [8]uint8{2}
	f.Nonce = env.input.ANonce
	return f
}

func (env *env) getPTK(sNonce [32]byte) *crypto.PTK {
	pmk, _ := crypto.PSK(env.input.PassPhrase, env.input.SSID)
	return crypto.DeriveKeys(pmk, env.input.STAAddr[:], env.input.BSSID[:], env.input.ANonce[:], sNonce[:])
}

func (env *env) SendToTestSubject(message *eapol.KeyFrame) error {
	// TODO(hahnr): Now that there is a Supplicant, use it.
	return env.fourway.HandleEAPOLKeyFrame(&Supplicant{}, message)
}

func (env *env) RetrieveTestSubjectResponse() *eapol.KeyFrame {
	response := env.transport.lastEapolKeyFrame
	env.transport.lastEapolKeyFrame = nil
	return response
}

// TransportMock implementation.

func (t *transportMock) SendEAPOLRequest(srcAddr [6]uint8, dstAddr [6]uint8, f *eapol.KeyFrame) error {
	copy(t.lastSrcAddr[:], srcAddr[:])
	copy(t.lastDstAddr[:], dstAddr[:])
	t.lastEapolKeyFrame = f
	defer func() { t.nextRequestError = nil }()
	return t.nextRequestError
}

func (t *transportMock) SendSetKeysRequest(keylist []mlme.SetKeyDescriptor) error {
	copy(t.lastKeyList, keylist)
	defer func() { t.nextRequestError = nil }()
	return t.nextRequestError
}

func GetRSNE(rsneData string) *elements.RSN {
	rsneBytes, _ := hex.DecodeString(rsneData)
	rsne, _ := elements.ParseRSN(rsneBytes)
	return rsne
}

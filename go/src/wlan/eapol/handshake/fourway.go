// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package handshake

import (
	"apps/wlan/eapol"
	"apps/wlan/wlan/elements"
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
	config FourWayConfig
}

func NewFourWay(config FourWayConfig) *FourWay {
	return &FourWay{config}
}

func (a *FourWay) HandleEAPOLKeyFrame(f *eapol.KeyFrame) error {
	// TODO(hahnr): Implement.
	return nil
}

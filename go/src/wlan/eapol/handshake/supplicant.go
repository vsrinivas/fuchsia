// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package handshake

import (
	"wlan/eapol"
	"fmt"
)

type Handshake interface {
	HandleEAPOLKeyFrame(s *Supplicant, f *eapol.KeyFrame) error
}

type Supplicant struct {
	handshake Handshake

	// TODO(hahnr): Keep track of PSK, PMK, KEK, KCK, Transport layer, Replay Counter etc.
}

func NewSupplicant(handshake Handshake) *Supplicant {
	return &Supplicant{
		handshake: handshake,
	}
}

func (s *Supplicant) HandleEAPOLKeyFrame(f *eapol.KeyFrame) error {
	if s.handshake == nil {
		return fmt.Errorf("no active handshake")
	}

	// TODO(hahnr): Perform integrity and state check on Key frame. E.g., is ACK set, MIC set, etc.
	// Note: Only non handshake specifc validations can be performed. E.g., we cannot verify that
	// the third message must have the secure bit set.

	return s.handshake.HandleEAPOLKeyFrame(s, f)
}
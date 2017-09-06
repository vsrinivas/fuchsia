// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package eapol

import (
	"errors"
	"log"
)

const debug = true

var (
	ErrUnspecifiedKeyType    = errors.New("KeyType not specified")
	ErrUnsupportedAuthMethod = errors.New("unsupported authentication method or corrupted EAPOL Key frame")
	ErrExpectedRequestBit    = errors.New("authenticator mustn't set Request bit")
)

type Config struct {
	MICBits     int
	KeyExchange KeyExchange
}

type Client struct {
	config Config
}

func NewClient(config Config) *Client {
	return &Client{config}
}

func (c *Client) HandleEAPOLFrame(frame []byte) {
	if c.config.KeyExchange == nil {
		return
	}

	hdr, err := ParseHeader(frame)
	if err != nil {
		if debug {
			log.Println(err)
		}
		return
	}

	switch hdr.PacketType {
	case PacketType_Key:
		keyFrame, err := ParseKeyFrame(hdr, frame[HeaderLen:], c.config.MICBits)
		if err != nil {
			if debug {
				log.Println(err)
			}
			return
		}
		err = c.handleEAPOLKeyFrame(keyFrame)
		if debug && err != nil {
			log.Println(err)
		}
	default:
		if debug {
			log.Printf("unknown EAPOL packet type: %d", hdr.PacketType)
		}
	}
}

func (c *Client) handleEAPOLKeyFrame(frame *KeyFrame) error {
	if !frame.Info.IsSet(KeyInfo_Type) {
		return ErrUnspecifiedKeyType
	}
	// ACK not being set indicates either a corrupted frame not sent from the authenticator or an
	// unsupported authentication method.
	if !frame.Info.IsSet(KeyInfo_ACK) {
		return ErrUnsupportedAuthMethod
	}
	// Must never be set from authenticator.
	if frame.Info.IsSet(KeyInfo_Request) {
		return ErrExpectedRequestBit
	}

	return c.config.KeyExchange.HandleEAPOLKeyFrame(frame)
}

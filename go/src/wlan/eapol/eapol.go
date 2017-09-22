// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package eapol

import (
	"log"
)

const debug = true

type Config struct {
	MICBits     int
	KeyExchange KeyExchange
}

// The EAPOL Client is a simple frame decoding and forwarding machinery. There is no, or very
// limited, frame verification done. The forwarding target should verify the frame's correctness.
// This intentionally provides no guarantees about the frame. For example there can be no assumption
// made whether the frame's MIC is valid or its content contains a none which was expected.
// The reason for this architecture is to not split up frame verification into multiple components,
// which if it was split up, could cause confusion about guarantees and expectations about a given
// frame.
type Client struct {
	config Config
}

func NewClient(config Config) *Client {
	return &Client{config}
}

func (c *Client) HandleEAPOLFrame(frame []byte) {
	if c.config.KeyExchange == nil {
		if debug {
			log.Println("no KeyExchange method configured")
		}
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
		err = c.config.KeyExchange.HandleEAPOLKeyFrame(keyFrame)
		if debug && err != nil {
			log.Println(err)
		}
	default:
		if debug {
			log.Printf("unknown EAPOL packet type: %d", hdr.PacketType)
		}
	}
}

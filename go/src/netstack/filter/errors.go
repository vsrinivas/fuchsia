// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package filter

type Error struct {
	Msg string
}

func (e Error) Error() string {
	return e.Msg
}

var (
	ErrBadTCPState     = Error{Msg: "Bad TCP state"}
	ErrPacketTooShort  = Error{Msg: "Packet is too short"}
	ErrUnknownProtocol = Error{Msg: "Unknown Protocol"}
)

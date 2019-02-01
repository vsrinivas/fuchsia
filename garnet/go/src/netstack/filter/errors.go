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
	ErrBadTCPState        = Error{Msg: "Bad TCP state"}
	ErrPacketTooShort     = Error{Msg: "Packet is too short"}
	ErrUnknownProtocol    = Error{Msg: "Unknown Protocol"}
	ErrBadProtocol        = Error{Msg: "Bad Protocol"}
	ErrUnknownAction      = Error{Msg: "Unknown Action"}
	ErrUnknownDirection   = Error{Msg: "Unknown Direction"}
	ErrUnknownAddressType = Error{Msg: "Unknown Address Type"}
	ErrBadAddress         = Error{Msg: "Bad address"}
)

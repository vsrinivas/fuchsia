// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package bindings2

// MessageHeaderSize is the size of the encoded message header.
const MessageHeaderSize int = 16

// MessageHeader is a header information for a message. This struct
// corresponds to the C type fidl_message_header_t.
type MessageHeader struct {
	Txid     uint32
	Reserved uint32
	Flags    uint32
	Ordinal  uint32
}

// Payload is an interface implemented by every FIDL structure.
type Payload interface {
	// InlineSize returns the inline size of the structure, without any
	// out-of-line structures.
	InlineSize() int

	// InlineAlignment returns the alignment of the structure.
	InlineAlignment() int
}

// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package bindings2

// MessageHeader is a header information for a message. This struct
// corresponds to the C type fidl_message_header_t.
type MessageHeader struct {
	Txid uint32
	Reserved uint32
	Flags uint32
	Ordinal uint32
}

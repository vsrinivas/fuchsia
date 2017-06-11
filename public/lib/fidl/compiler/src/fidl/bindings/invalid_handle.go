// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package bindings

import (
	"fidl/system"
)

// InvalidHandle is a handle that will always be invalid.
type InvalidHandle struct {
}

func (h *InvalidHandle) IsValid() bool {
	return false
}

func (h *InvalidHandle) ToUntypedHandle() system.UntypedHandle {
	return h
}

func (h *InvalidHandle) ToChannelHandle() system.ChannelHandle {
	return h
}

func (h *InvalidHandle) ToVmoHandle() system.VmoHandle {
	return h
}

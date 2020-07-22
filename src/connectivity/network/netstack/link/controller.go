// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package link

type Controller interface {
	Up() error
	Down() error
	SetPromiscuousMode(bool) error
}

type Observer interface {
	SetOnLinkClosed(func())
	SetOnLinkOnlineChanged(func(bool))
}

// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//+build !fuchsia

package fswatch

import (
	"github.com/fsnotify/fsnotify"
)

type Watcher = fsnotify.Watcher

var NewWatcher = fsnotify.NewWatcher

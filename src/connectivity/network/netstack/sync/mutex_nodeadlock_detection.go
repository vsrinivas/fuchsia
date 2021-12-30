// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//go:build !deadlock_detection
// +build !deadlock_detection

package sync

import "sync"

type Mutex = sync.Mutex
type RWMutex = sync.RWMutex

// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//go:build !tracing
// +build !tracing

package trace

func GetCurrentProcessKoid() uint64 {
	return 0
}

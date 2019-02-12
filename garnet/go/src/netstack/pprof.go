// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//+build pprof

package netstack

import (
	"net/http"
	_ "net/http/pprof"

	"syslog/logger"
)

func init() {
	go func() {
		logger.Infof("starting http pprof server on port 6060")
		logger.Infof(http.ListenAndServe(":6060", nil))
	}()
}

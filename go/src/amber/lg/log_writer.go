// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package lg

type LoggerWriter struct {
	Logger
}

func (l *LoggerWriter) Write(data []byte) (n int, err error) {
	if err := l.Infof("%s", data); err != nil {
		return 0, err
	}

	return len(data), err
}

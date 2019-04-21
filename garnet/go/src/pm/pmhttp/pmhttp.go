// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package pmhttp

import (
	"compress/gzip"
	"log"
	"net/http"
)

type GZIPWriter struct {
	http.ResponseWriter
	*gzip.Writer
}

func (w *GZIPWriter) Header() http.Header {
	return w.ResponseWriter.Header()
}

func (w *GZIPWriter) Write(b []byte) (int, error) {
	return w.Writer.Write(b)
}

func (w *GZIPWriter) Flush() {
	if err := w.Writer.Flush(); err != nil {
		panic(err)
	}
	if f, ok := w.ResponseWriter.(http.Flusher); ok {
		f.Flush()
	} else {
		log.Fatal("server misconfigured, can not flush")
	}
}

type LoggingWriter struct {
	http.ResponseWriter
	Status int
}

func (lw *LoggingWriter) WriteHeader(status int) {
	lw.Status = status
	lw.ResponseWriter.WriteHeader(status)
}

func (lw *LoggingWriter) Flush() {
	if f, ok := lw.ResponseWriter.(http.Flusher); ok {
		f.Flush()
	} else {
		log.Fatal("server misconfigured, can not flush")
	}
}

var _ http.Flusher = &LoggingWriter{}
var _ http.Flusher = &GZIPWriter{}

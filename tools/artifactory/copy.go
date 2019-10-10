// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// found in the LICENSE file.

package artifactory

import (
	"context"
	"io"
	"strings"

	"go.fuchsia.dev/fuchsia/tools/lib/logger"
)

const (
	// Err412Msg is substring of an error message returned that indicates a 412
	// HTTP response.
	err412Msg = "Error 412"
)

// Copy abstracts writing a reader to the Cloud, `w` canonically being a
// storage.Writer. It handles cases relating to the asynchronously
// returning on error on Close() or returning a 412, which represents the case
// where an object fails the precondition of not existing at the time of the
// write.
func Copy(ctx context.Context, objName string, r io.Reader, w io.WriteCloser, chunkSize int64) error {
	var err error
	for {
		if _, err = io.CopyN(w, r, chunkSize); err != nil {
			break
		}
	}

	// If the precondition of the object not existing is not met on write (i.e.,
	// at the time of the write the object is there), then the server will respond
	// with a 412. (See
	// https://cloud.google.com/storage/docs/json_api/v1/status-codes and
	// https://tools.ietf.org/html/rfc7232#section-4.2.)
	// We do not report this as an error, however, as the associated object might
	// have been created after having checked its non-existence - and we wish to
	// be resilient in the event of such a race.
	handleOkayError := func(err error) error {
		preconditionNotMet := err != nil && strings.Contains(err.Error(), err412Msg)
		if err == io.EOF || preconditionNotMet {
			if preconditionNotMet {
				logger.Warningf(ctx, "object %q: created after its non-existence check", objName)
			}
			return nil
		}
		return err
	}

	// Writes happen asynchronously, and so a nil may be returned while the write
	// goes on to fail. It is recommended in
	// https://godoc.org/cloud.google.com/go/storage#Writer.Write
	// to return the value of Close() to detect the success of the write.
	if handleOkayError(err) == nil {
		return handleOkayError(w.Close())
	}
	w.Close()
	return err
}

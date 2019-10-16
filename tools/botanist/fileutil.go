// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package botanist

import (
	"archive/tar"
	"bytes"
	"context"
	"fmt"
	"sync"
	"time"

	"go.fuchsia.dev/fuchsia/tools/lib/retry"
	"go.fuchsia.dev/fuchsia/tools/net/tftp"
)

// FetchAndArchiveFile fetches a remote file via TFTP from a given node, and
// writes it an archive.
func FetchAndArchiveFile(ctx context.Context, t tftp.Client, tw *tar.Writer, path, name string, lock *sync.Mutex) error {
	return retry.Retry(ctx, retry.WithMaxRetries(retry.NewConstantBackoff(time.Second), 3), func() error {
		var err error
		var reader *bytes.Reader
		for {
			reader, err = t.Read(ctx, path)
			switch err {
			case nil:
			case tftp.ErrShouldWait:
				time.Sleep(time.Second)
				continue
			default:
				return fmt.Errorf("failed to receive file %s: %s", path, err)
			}
			break
		}
		lock.Lock()
		defer lock.Unlock()
		hdr := &tar.Header{
			Name: name,
			Size: int64(reader.Len()),
			Mode: 0666,
		}
		if err := tw.WriteHeader(hdr); err != nil {
			return err
		}
		_, err = reader.WriteTo(tw)
		return err
	}, nil)
}

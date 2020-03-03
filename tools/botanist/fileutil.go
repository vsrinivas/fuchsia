// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package botanist

import (
	"archive/tar"
	"bytes"
	"context"
	"fmt"
	"io"
	"path/filepath"
	"sync"
	"time"

	"go.fuchsia.dev/fuchsia/tools/lib/osmisc"
	"go.fuchsia.dev/fuchsia/tools/lib/retry"
	"go.fuchsia.dev/fuchsia/tools/net/tftp"
)

// FetchAndCopyFile fetches a remote file via TFTP from a given node, and
// writes it to an archive or output directory.
func FetchAndCopyFile(ctx context.Context, t tftp.Client, tw *tar.Writer, path, name string, lock *sync.Mutex, outDir string) error {
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
		var w io.WriteCloser
		if tw != nil {
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
			w = tw
		} else {
			outputFile := filepath.Join(outDir, name)
			w, err = osmisc.CreateFile(outputFile)
			if err != nil {
				return fmt.Errorf("failed to create file: %v", err)
			}
			defer w.Close()
		}
		_, err = reader.WriteTo(w)
		return err
	}, nil)
}

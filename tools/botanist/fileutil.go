// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package botanist

import (
	"bytes"
	"context"
	"fmt"
	"path/filepath"
	"time"

	"go.fuchsia.dev/fuchsia/tools/botanist/constants"
	"go.fuchsia.dev/fuchsia/tools/lib/osmisc"
	"go.fuchsia.dev/fuchsia/tools/lib/retry"
	"go.fuchsia.dev/fuchsia/tools/net/tftp"
)

// FetchAndCopyFile fetches a remote file via TFTP from a given node, and
// writes it to an output directory.
func FetchAndCopyFile(ctx context.Context, t tftp.Client, path, name, outDir string) error {
	return retry.Retry(ctx, retry.WithMaxAttempts(retry.NewConstantBackoff(time.Second*5), 3), func() error {
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
				return fmt.Errorf("%s %s: %s", constants.FailedToReceiveFileMsg, path, err)
			}
			break
		}
		outputFile := filepath.Join(outDir, name)
		w, err := osmisc.CreateFile(outputFile)
		if err != nil {
			return fmt.Errorf("failed to create file: %v", err)
		}
		defer w.Close()
		_, err = reader.WriteTo(w)
		return err
	}, nil)
}

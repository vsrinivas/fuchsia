// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
package botanist

import (
	"archive/tar"
	"bytes"
	"context"
	"fmt"
	"log"
	"net"
	"os"
	"path"
	"path/filepath"
	"time"

	"fuchsia.googlesource.com/tools/retry"
	"fuchsia.googlesource.com/tools/tftp"
)

// TFTPFiles is an ordered map for files which will be TFTP'd over to zedboot.
type TFTPFiles struct {
	remotes []string
	locals  [][]string
}

// Set adds a configuration of what files to send for to the remote name.
func (f *TFTPFiles) Set(remote string, locals ...string) {
	f.remotes = append(f.remotes, remote)
	f.locals = append(f.locals, locals)
}

// ForEach calls a callback for each file to send in a filesMap.
func (f *TFTPFiles) ForEach(cb func(remote, local string) error) error {
	for i := 0; i < len(f.remotes); i++ {
		for j := 0; j < len(f.locals[i]); j++ {
			if err := cb(f.remotes[i], f.locals[i][j]); err != nil {
				return err
			}
		}
	}
	return nil
}

// Transfer sends the files over TFTP to a node at a given address.
func (f *TFTPFiles) Transfer(ctx context.Context, client *tftp.Client, addr *net.UDPAddr) error {
	type file struct {
		*os.File
		os.FileInfo
	}
	// Open and Stat all files inside the TFTPFiles structure.
	files := make(map[string]file)
	err := f.ForEach(func(remote, local string) error {
		f, err := os.Open(local)
		if err != nil {
			return fmt.Errorf("cannot open %s: %v\n", local, err)
		}
		fi, err := f.Stat()
		if err != nil {
			return fmt.Errorf("cannot stat %s: %v\n", local, err)
		}
		files[remote+local] = file{File: f, FileInfo: fi}
		return nil
	})
	// Set up a defer for closing all the successfully opened files.
	defer func() {
		for _, f := range files {
			f.File.Close()
		}
	}()
	if err != nil {
		return err
	}
	// Attempt the whole process of sending every file over and retry on failure of any file.
	return retry.Retry(ctx, retry.WithMaxRetries(retry.NewConstantBackoff(time.Second), 10), func() error {
		return f.ForEach(func(remote, local string) error {
			// Attempt to send a file. If the server tells us we need to wait, then try
			// again as long as it keeps telling us this. ErrShouldWait implies the server
			// is still responding and will eventually be able to handle our request.
			f := files[remote+local]
			for {
				fmt.Printf("attempting to send %s=%s...", remote, filepath.Base(local))
				reader, err := client.Send(addr, remote, f.FileInfo.Size())
				switch {
				case err == tftp.ErrShouldWait:
					// The target is busy, so let's sleep for a bit before
					// trying again, otherwise we'll be wasting cycles and
					// printing too often.
					fmt.Println("target is busy, retrying in one second")
					select {
					case <-ctx.Done():
						return nil
					case <-time.After(time.Second):
						continue
					}
				case err != nil:
					fmt.Println("found error, starting from the top")
					return fmt.Errorf("failed to send %s: %v\n", remote, err)
				}
				if _, err := reader.ReadFrom(f.File); err != nil {
					fmt.Println("unable to read from file, retrying")
					return fmt.Errorf("failed to send %s data: %v\n", local, err)
				}
				break
			}
			fmt.Println("done")
			return nil
		})
	})
}

// TransferCmdlineArgs sends command-line arguments to a file over TFTP to a node at a given address.
func TransferCmdlineArgs(client *tftp.Client, addr *net.UDPAddr, cmdlineArgs []string) error {
	if len(cmdlineArgs) > 0 {
		var b bytes.Buffer
		for _, arg := range cmdlineArgs {
			fmt.Fprintf(&b, "%s\n", arg)
		}

		log.Printf("sending cmdline \"%s\"", b.String())

		reader, err := client.Send(addr, CmdlineFilename, int64(b.Len()))
		if err != nil {
			return fmt.Errorf("failed to send cmdline args: %v\n", err)
		}
		if _, err := reader.ReadFrom(bytes.NewReader(b.Bytes())); err != nil {
			return fmt.Errorf("failed to read cmdline data: %v\n", err)
		}
	}
	return nil
}

func TransferAndWriteFileToTar(client *tftp.Client, tftpAddr *net.UDPAddr, tw *tar.Writer, testResultsDir string, outputFile string) error {
	writer, err := client.Receive(tftpAddr, path.Join(testResultsDir, outputFile))
	if err != nil {
		return fmt.Errorf("failed to receive file %s: %v\n", outputFile, err)
	}
	hdr := &tar.Header{
		Name: outputFile,
		Size: writer.(tftp.Session).Size(),
		Mode: 0666,
	}
	if err := tw.WriteHeader(hdr); err != nil {
		return fmt.Errorf("failed to write file header: %v\n", err)
	}
	if _, err := writer.WriteTo(tw); err != nil {
		return fmt.Errorf("failed to write file content: %v\n", err)
	}

	return nil
}

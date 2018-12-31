// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package botanist

import (
	"bytes"
	"context"
	"fmt"
	"io"
	"log"
	"net"
	"os"
	"sort"
	"time"

	"fuchsia.googlesource.com/tools/retry"
	"fuchsia.googlesource.com/tools/tftp"
)

const (
	// Special image names recognized by fuchsia's netsvc.
	cmdlineNetsvcName = "<<netboot>>cmdline"
	sshNetsvcName     = "<<netboot>>authorized_keys"
	kernelNetsvcName  = "<<netboot>>kernel.bin"
	fvmNetsvcName     = "<<image>>sparse.fvm"
	efiNetsvcName     = "<<image>>efi.img"
	kerncNetsvcName   = "<<image>>kernc.img"
	zirconANetsvcName = "<<image>>zircona.img"
	zirconBNetsvcName = "<<image>>zirconb.img"
	zirconRNetsvcName = "<<image>>zirconr.img"
)

// The order in which bootserver serves images to netsvc.
// Must be a subsequence of the order given in
// https://fuchsia.googlesource.com/zircon/+/master/system/host/bootserver/bootserver.c
var bootserverImgOrder = map[string]int{
	"storage-sparse": 1,
	"efi":            2,
	"zircon-vboot":   3,
	"zircon-a":       4,
	"zircon-b":       5,
	"zircon-r":       6,
}

// GetNetsvcName returns the associated name recognized by fuchsia's netsvc.
func GetNetsvcName(img Image) (string, bool) {
	m := map[string]string{
		"storage-sparse": fvmNetsvcName,
		"efi":            efiNetsvcName,
		"zircon-a":       zirconANetsvcName,
		"zircon-b":       zirconBNetsvcName,
		"zircon-r":       zirconRNetsvcName,
		"zircon-vboot":   kerncNetsvcName,
	}
	name, ok := m[img.Name]
	return name, ok
}

// Returns whether the list of strings containst "--boot". If an image has such a switch
// in its PaveArgs or NetbootArgs, it should be treated as a kernel image.
func containsBootSwitch(strs []string) bool {
	for _, s := range strs {
		if s == "--boot" {
			return true
		}
	}
	return false
}

// Pave transfers all images from imgs that are required for paving to the node specified by tftpAddr.
func Pave(ctx context.Context, t *tftp.Client, tftpAddr *net.UDPAddr, imgs []Image, cmdlineArgs []string, sshKey string) error {
	// Key on whether bootserver paving args are present to determine if the image is used
	// to pave.
	var kernel *Image
	paveImgs := []Image{}
	for i, _ := range imgs {
		if len(imgs[i].PaveArgs) > 0 {
			if containsBootSwitch(imgs[i].PaveArgs) {
				kernel = &imgs[i]
			}
			paveImgs = append(paveImgs, imgs[i])
		}
	}
	sort.Slice(paveImgs, func(i, j int) bool {
		return bootserverImgOrder[paveImgs[i].Name] < bootserverImgOrder[paveImgs[j].Name]
	})
	return transfer(ctx, t, tftpAddr, paveImgs, kernel, cmdlineArgs, sshKey)
}

// Netboot transfers all images from imgs that are required for netbooting to the node specified by tftpAddr.
func Netboot(ctx context.Context, t *tftp.Client, tftpAddr *net.UDPAddr, imgs []Image, cmdlineArgs []string, sshKey string) error {
	// Key on whether bootserver netbooting args are present to determine if the image is
	// used to netboot.
	var kernel *Image
	for i, _ := range imgs {
		if len(imgs[i].NetbootArgs) > 0 {
			if containsBootSwitch(imgs[i].NetbootArgs) {
				kernel = &imgs[i]
			}
		}
	}
	return transfer(ctx, t, tftpAddr, []Image{}, kernel, cmdlineArgs, sshKey)
}

// A struct represenitng a file to send per the netboot protocol.
type netsvcFile struct {
	reader io.Reader
	size   int64
	path   string
	name   string
}

func (f netsvcFile) close() error {
	closer, ok := (f.reader).(io.Closer)
	if ok {
		return closer.Close()
	}
	return nil
}

func openNetsvcFile(path, name string) (*netsvcFile, error) {
	fd, err := os.Open(path)
	if err != nil {
		fd.Close()
		return nil, err
	}
	fi, err := fd.Stat()
	if err != nil {
		fd.Close()
		return nil, err
	}
	return &netsvcFile{reader: fd, size: fi.Size(), name: name}, nil
}

// Transfers images with the appropriate netboot prefixes over TFTP to a node at a given
// address.
func transfer(ctx context.Context, t *tftp.Client, tftpAddr *net.UDPAddr, imgs []Image, kernel *Image, cmdlineArgs []string, sshKey string) error {
	// Prepare all files to be tranferred, minding the order, which follows that of the
	// bootserver host tool.
	netsvcFiles := []*netsvcFile{}

	if len(cmdlineArgs) > 0 {
		var buf bytes.Buffer
		for _, arg := range cmdlineArgs {
			fmt.Fprintf(&buf, "%s\n", arg)
		}
		cmdlineNetsvcFile := &netsvcFile{
			reader: bytes.NewReader(buf.Bytes()),
			size:   int64(buf.Len()),
			name:   cmdlineNetsvcName,
		}
		netsvcFiles = append(netsvcFiles, cmdlineNetsvcFile)
	}

	for _, img := range imgs {
		netsvcName, ok := GetNetsvcName(img)
		if !ok {
			return fmt.Errorf("Could not find associated netsvc name for %s", img.Name)
		}
		imgNetsvcFile, err := openNetsvcFile(img.Path, netsvcName)
		if err != nil {
			return err
		}
		defer imgNetsvcFile.close()
		netsvcFiles = append(netsvcFiles, imgNetsvcFile)
	}

	if sshKey != "" {
		sshNetsvcFile, err := openNetsvcFile(sshKey, sshNetsvcName)
		if err != nil {
			return err
		}
		defer sshNetsvcFile.close()
		netsvcFiles = append(netsvcFiles, sshNetsvcFile)
	}

	if kernel != nil {
		kernelNetsvcFile, err := openNetsvcFile(kernel.Path, kernelNetsvcName)
		if err != nil {
			return err
		}
		defer kernelNetsvcFile.close()
		netsvcFiles = append(netsvcFiles, kernelNetsvcFile)
	}

	// Attempt the whole process of sending every file over and retry on failure of any file.
	// This behavior more closely aligns with that of the bootserver.
	return retry.Retry(ctx, retry.WithMaxRetries(retry.NewConstantBackoff(time.Second), 10), func() error {
		for _, f := range netsvcFiles {
			select {
			case <-ctx.Done():
				return nil
			default:
			}
			// Attempt to send a file. If the server tells us we need to wait, then try
			// again as long as it keeps telling us this. ErrShouldWait implies the server
			// is still responding and will eventually be able to handle our request.
			for {
				log.Printf("attempting to send %s...\n", f.name)
				reader, err := t.Send(tftpAddr, f.name, f.size)
				switch {
				case err == tftp.ErrShouldWait:
					// The target is busy, so let's sleep for a bit before
					// trying again, otherwise we'll be wasting cycles and
					// printing too often.
					log.Printf("target is busy, retrying in one second\n")
					select {
					case <-time.After(time.Second):
						continue
					}
				case err != nil:
					log.Printf("failed to send %s; starting from the top: %v\n", f.name, err)
					return err
				}
				if _, err := reader.ReadFrom(f.reader); err != nil {
					log.Printf("unable to read from %s; retrying: %v\n", f.name, err)
					return err
				}
				break
			}
			log.Printf("done\n")
		}
		return nil
	}, nil)
}

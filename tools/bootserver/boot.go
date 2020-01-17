// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package bootserver

import (
	"bytes"
	"context"
	"errors"
	"fmt"
	"io"
	"log"
	"os"
	"path/filepath"
	"sort"
	"sync"
	"time"

	"go.fuchsia.dev/fuchsia/tools/lib/iomisc"
	"go.fuchsia.dev/fuchsia/tools/lib/retry"
	"go.fuchsia.dev/fuchsia/tools/net/netboot"
	"go.fuchsia.dev/fuchsia/tools/net/tftp"
	"golang.org/x/crypto/ssh"
)

const (
	// Special image names recognized by fuchsia's netsvc.
	authorizedKeysNetsvcName = "<<image>>authorized_keys"
	bootloaderNetsvcName     = "<<image>>bootloader.img"
	cmdlineNetsvcName        = "<<netboot>>cmdline"
	efiNetsvcName            = "<<image>>efi.img"
	fvmNetsvcName            = "<<image>>sparse.fvm"
	kerncNetsvcName          = "<<image>>kernc.img"
	kernelNetsvcName         = "<<netboot>>kernel.bin"
	vbmetaANetsvcName        = "<<image>>vbmetaa.img"
	vbmetaBNetsvcName        = "<<image>>vbmetab.img"
	vbmetaRNetsvcName        = "<<image>>vbmetar.img"
	zirconANetsvcName        = "<<image>>zircona.img"
	zirconBNetsvcName        = "<<image>>zirconb.img"
	zirconRNetsvcName        = "<<image>>zirconr.img"
)

// Maps bootserver argument to a corresponding netsvc name.
var bootserverArgToName = map[string]string{
	"--boot":       kernelNetsvcName,
	"--bootloader": bootloaderNetsvcName,
	"--efi":        efiNetsvcName,
	"--fvm":        fvmNetsvcName,
	"--kernc":      kerncNetsvcName,
	"--vbmetaa":    vbmetaANetsvcName,
	"--vbmetab":    vbmetaBNetsvcName,
	"--vbmetar":    vbmetaRNetsvcName,
	"--zircona":    zirconANetsvcName,
	"--zirconb":    zirconBNetsvcName,
	"--zirconr":    zirconRNetsvcName,
}

// Maps netsvc name to the index at which the corresponding file should be transferred if
// present. The indices correspond to the ordering given in
// https://go.fuchsia.dev/zircon/+/master/system/host/bootserver/bootserver.c
var transferOrder = map[string]int{
	cmdlineNetsvcName:        1,
	fvmNetsvcName:            2,
	bootloaderNetsvcName:     3,
	efiNetsvcName:            4,
	kerncNetsvcName:          5,
	zirconANetsvcName:        6,
	zirconBNetsvcName:        7,
	zirconRNetsvcName:        8,
	vbmetaANetsvcName:        9,
	vbmetaBNetsvcName:        10,
	vbmetaRNetsvcName:        11,
	authorizedKeysNetsvcName: 12,
	kernelNetsvcName:         13,
}

func downloadImages(imgs []Image) ([]Image, func() error, error) {
	var newImgs []Image
	// Copy each in a goroutine for efficiency's sake.
	errs := make(chan error, len(imgs))
	var mux sync.Mutex
	var wg sync.WaitGroup
	for _, img := range imgs {
		wg.Add(1)
		go func(img Image) {
			defer wg.Done()
			if img.Reader != nil {
				f, err := downloadAndOpenImage(img.Name, img)
				if err != nil {
					errs <- err
					return
				}
				fi, err := f.Stat()
				if err != nil {
					f.Close()
					errs <- err
					return
				}
				mux.Lock()
				newImgs = append(newImgs, Image{
					Name:   img.Name,
					Reader: f,
					Size:   fi.Size(),
					Args:   img.Args,
				})
				mux.Unlock()
			}
		}(img)
	}
	wg.Wait()

	closeFunc := func() error { return closeImages(newImgs) }
	select {
	case err := <-errs:
		closeImages(newImgs)
		return nil, closeFunc, err
	default:
		return newImgs, closeFunc, nil
	}
}

func downloadAndOpenImage(dest string, img Image) (*os.File, error) {
	f, ok := img.Reader.(*os.File)
	if ok {
		return f, nil
	}

	// If the file already exists at dest, just open and return the file instead of downloading again.
	// This will avoid duplicate downloads from catalyst (which calls the bootserver tool) and botanist.
	if _, err := os.Stat(dest); !os.IsNotExist(err) {
		return os.Open(dest)
	}

	f, err := os.Create(dest)
	if err != nil {
		return nil, err
	}

	// Log progress to avoid hitting I/O timeout in case of slow transfers.
	ticker := time.NewTicker(30 * time.Second)
	defer ticker.Stop()
	go func() {
		for range ticker.C {
			log.Printf("transferring %s...\n", filepath.Base(dest))
		}
	}()

	if _, err := io.Copy(f, iomisc.ReaderAtToReader(img.Reader)); err != nil {
		f.Close()
		return nil, fmt.Errorf("failed to copy image %q to %q: %v", img.Name, dest, err)
	}
	if err := f.Sync(); err != nil {
		f.Close()
		return nil, err
	}
	return f, nil
}

// Boot prepares and boots a device at the given IP address.
func Boot(ctx context.Context, t tftp.Client, imgs []Image, cmdlineArgs []string, signers []ssh.Signer) error {
	var files []*netsvcFile
	if len(cmdlineArgs) > 0 {
		var buf bytes.Buffer
		for _, arg := range cmdlineArgs {
			fmt.Fprintf(&buf, "%s\n", arg)
		}
		reader := bytes.NewReader(buf.Bytes())
		cmdlineFile, err := newNetsvcFile(cmdlineNetsvcName, reader, reader.Size())
		if err != nil {
			return err
		}
		files = append(files, cmdlineFile)
	}

	// This is needed because imgs from GCS are compressed and we cannot get the correct size of the uncompressed images, so we have to download them first.
	// TODO(ihuh): We should enable this step as a command line option.
	imgs, closeFunc, err := downloadImages(imgs)
	if err != nil {
		return err
	}
	defer closeFunc()

	for _, img := range imgs {
		for _, arg := range img.Args {
			name, ok := bootserverArgToName[arg]
			if !ok {
				return fmt.Errorf("unrecognized bootserver argument found: %s", arg)
			}
			imgFile, err := newNetsvcFile(name, img.Reader, img.Size)
			if err != nil {
				return err
			}
			files = append(files, imgFile)
		}
	}

	// Convert the authorized keys into a netsvc file.
	if len(signers) > 0 {
		var authorizedKeys []byte
		for _, s := range signers {
			authorizedKey := ssh.MarshalAuthorizedKey(s.PublicKey())
			authorizedKeys = append(authorizedKeys, authorizedKey...)
		}
		reader := bytes.NewReader(authorizedKeys)
		authorizedKeysFile, err := newNetsvcFile(authorizedKeysNetsvcName, reader, reader.Size())
		if err != nil {
			return err
		}
		files = append(files, authorizedKeysFile)
	}

	sort.Slice(files, func(i, j int) bool {
		return files[i].index < files[j].index
	})

	if len(files) == 0 {
		return errors.New("no files to transfer")
	}
	if err := transfer(ctx, t, files); err != nil {
		return err
	}

	// If we do not load a kernel into RAM, then we reboot back into the first kernel
	// partition; else we boot directly from RAM.
	// TODO(ZX-2069): Eventually, no such kernel should be present.
	hasRAMKernel := files[len(files)-1].name == kernelNetsvcName
	n := netboot.NewClient(time.Second)
	if hasRAMKernel {
		// Try to send the boot command a few times, as there's no ack, so it's
		// not possible to tell if it's successfully booted or not.
		for i := 0; i < 5; i++ {
			n.Boot(t.RemoteAddr())
		}
	}
	return n.Reboot(t.RemoteAddr())
}

// A file to send to netsvc.
type netsvcFile struct {
	name   string
	reader io.ReaderAt
	index  int
	size   int64
}

func newNetsvcFile(name string, reader io.ReaderAt, size int64) (*netsvcFile, error) {
	idx, ok := transferOrder[name]
	if !ok {
		return nil, fmt.Errorf("unrecognized name: %s", name)
	}
	return &netsvcFile{
		reader: reader,
		name:   name,
		index:  idx,
		size:   size,
	}, nil
}

// Transfers files over TFTP to a node at a given address.
func transfer(ctx context.Context, t tftp.Client, files []*netsvcFile) error {
	// Attempt the whole process of sending every file over and retry on failure of any file.
	// This behavior more closely aligns with that of the bootserver.
	return retry.Retry(ctx, retry.WithMaxRetries(retry.NewConstantBackoff(time.Second), 20), func() error {
		for _, f := range files {
			// Attempt to send a file. If the server tells us we need to wait, then try
			// again as long as it keeps telling us this. ErrShouldWait implies the server
			// is still responding and will eventually be able to handle our request.
			log.Printf("attempting to send %s...\n", f.name)
			for {
				if ctx.Err() != nil {
					return nil
				}
				err := t.Write(ctx, f.name, f.reader, f.size)
				switch err {
				case nil:
				case tftp.ErrShouldWait:
					// The target is busy, so let's sleep for a bit before
					// trying again, otherwise we'll be wasting cycles and
					// printing too often.
					log.Printf("target is busy, retrying in one second\n")
					time.Sleep(time.Second)
					continue
				default:
					log.Printf("failed to send %s; starting from the top: %v\n", f.name, err)
					return err
				}
				break
			}
			log.Printf("done\n")
		}
		return nil
	}, nil)
}

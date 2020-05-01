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
	"io/ioutil"
	"os"
	"path/filepath"
	"sort"
	"strings"
	"sync"
	"time"

	"go.fuchsia.dev/fuchsia/tools/lib/iomisc"
	"go.fuchsia.dev/fuchsia/tools/lib/logger"
	"go.fuchsia.dev/fuchsia/tools/lib/retry"
	"go.fuchsia.dev/fuchsia/tools/net/netboot"
	"go.fuchsia.dev/fuchsia/tools/net/tftp"
	"golang.org/x/crypto/ssh"
	"golang.org/x/sync/errgroup"
)

const (
	// Special image names recognized by fuchsia's netsvc.
	authorizedKeysNetsvcName = "<<image>>authorized_keys"
	boardInfoNetsvcName      = "<<image>>board_info"
	bootloaderNetsvcName     = "<<image>>bootloader.img"
	cmdlineNetsvcName        = "<<netboot>>cmdline"
	efiNetsvcName            = "<<image>>efi.img"
	firmwareNetsvcPrefix     = "<<image>>firmware_"
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
var bootserverArgToNameMap = map[string]string{
	"--boot":       kernelNetsvcName,
	"--bootloader": bootloaderNetsvcName,
	"--efi":        efiNetsvcName,
	"--firmware":   firmwareNetsvcPrefix,
	"--fvm":        fvmNetsvcName,
	"--kernc":      kerncNetsvcName,
	"--vbmetaa":    vbmetaANetsvcName,
	"--vbmetab":    vbmetaBNetsvcName,
	"--vbmetar":    vbmetaRNetsvcName,
	"--zircona":    zirconANetsvcName,
	"--zirconb":    zirconBNetsvcName,
	"--zirconr":    zirconRNetsvcName,
}

func bootserverArgToName(arg string) (string, bool) {
	// Check for a typed firmware arg "--firmare-<type>".
	if strings.HasPrefix(arg, "--firmware-") {
		fwType := strings.TrimPrefix(arg, "--firmware-")
		return firmwareNetsvcPrefix + fwType, true
	}

	name, exists := bootserverArgToNameMap[arg]
	return name, exists
}

// Maps netsvc name to the index at which the corresponding file should be transferred if
// present. The indices correspond to the ordering given in
// https://go.fuchsia.dev/zircon/+/master/system/host/bootserver/bootserver.c
var transferOrderMap = map[string]int{
	cmdlineNetsvcName:        1,
	fvmNetsvcName:            2,
	bootloaderNetsvcName:     3,
	firmwareNetsvcPrefix:     4,
	efiNetsvcName:            5,
	kerncNetsvcName:          6,
	zirconANetsvcName:        7,
	zirconBNetsvcName:        8,
	zirconRNetsvcName:        9,
	vbmetaANetsvcName:        10,
	vbmetaBNetsvcName:        11,
	vbmetaRNetsvcName:        12,
	authorizedKeysNetsvcName: 13,
	kernelNetsvcName:         14,
}

func transferOrder(name string) (int, bool) {
	// Ordering doesn't matter among different types of firmware, they can
	// all use the same index.
	if strings.HasPrefix(name, firmwareNetsvcPrefix) {
		name = firmwareNetsvcPrefix
	}

	index, exists := transferOrderMap[name]
	return index, exists
}

// Returns true if this image is OK to skip on error rather than failing out.
// Sometimes this is necessary in order to be able to write to an older netsvc
// which may not know about newer image types.
func skipOnTransferError(name string) bool {
	return strings.HasPrefix(name, firmwareNetsvcPrefix)
}

func downloadImagesToDir(ctx context.Context, dir string, imgs []Image) ([]Image, func() error, error) {
	// Copy each in a goroutine for efficiency's sake.
	eg := errgroup.Group{}
	mux := sync.Mutex{}
	var newImgs []Image
	for _, img := range imgs {
		if len(img.Args) == 0 || img.Reader == nil {
			continue
		}
		img := img
		eg.Go(func() error {
			f, err := downloadAndOpenImage(ctx, filepath.Join(dir, img.Name), img)
			if err != nil {
				return err
			}
			fi, err := f.Stat()
			if err != nil {
				f.Close()
				return err
			}
			mux.Lock()
			newImgs = append(newImgs, Image{
				Name:   img.Name,
				Reader: f,
				Size:   fi.Size(),
				Args:   img.Args,
			})
			mux.Unlock()
			return nil
		})
	}

	if err := eg.Wait(); err != nil {
		closeImages(newImgs)
		return nil, noOpClose, err
	}
	return newImgs, func() error { return closeImages(newImgs) }, nil
}

func downloadAndOpenImage(ctx context.Context, dest string, img Image) (*os.File, error) {
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
			logger.Infof(ctx, "transferring %s...\n", filepath.Base(dest))
		}
	}()

	if _, err := io.Copy(f, iomisc.ReaderAtToReader(img.Reader)); err != nil {
		f.Close()
		return nil, fmt.Errorf("failed to copy image %q to %q: %w", img.Name, dest, err)
	}
	if err := f.Close(); err != nil {
		return nil, err
	}
	return os.Open(dest)
}

// Prepares and transfers images to the given client.
// Returns whether the images contained a RAM kernel or not, or error on failure.
func transferImages(ctx context.Context, t tftp.Client, imgs []Image, cmdlineArgs []string, signers []ssh.Signer) (bool, error) {
	var files []*netsvcFile
	if len(cmdlineArgs) > 0 {
		var buf bytes.Buffer
		for _, arg := range cmdlineArgs {
			fmt.Fprintf(&buf, "%s\n", arg)
		}
		reader := bytes.NewReader(buf.Bytes())
		cmdlineFile, err := newNetsvcFile(cmdlineNetsvcName, reader, reader.Size())
		if err != nil {
			return false, err
		}
		files = append(files, cmdlineFile)
	}

	// This is needed because imgs from GCS are compressed and we cannot get the correct size of the uncompressed images, so we have to download them first.
	// TODO(ihuh): We should enable this step as a command line option.
	workdir, err := ioutil.TempDir("", "working-dir")
	if err != nil {
		return false, err
	}
	defer os.RemoveAll(workdir)
	imgs, closeFunc, err := downloadImagesToDir(ctx, workdir, imgs)
	if err != nil {
		return false, err
	}
	defer closeFunc()

	for _, img := range imgs {
		for _, arg := range img.Args {
			name, ok := bootserverArgToName(arg)
			if !ok {
				return false, fmt.Errorf("unrecognized bootserver argument found: %s", arg)
			}
			imgFile, err := newNetsvcFile(name, img.Reader, img.Size)
			if err != nil {
				return false, err
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
			return false, err
		}
		files = append(files, authorizedKeysFile)
	}

	sort.Slice(files, func(i, j int) bool {
		// Firmware files share an index. Functionally the order doesn't matter
		// but it makes test verification a lot simpler if we apply a fixed
		// ordering, so sort equal index files by name.
		if files[i].index == files[j].index {
			return files[i].name < files[j].name
		}
		return files[i].index < files[j].index
	})

	if len(files) == 0 {
		return false, errors.New("no files to transfer")
	}
	if err := transfer(ctx, t, files); err != nil {
		return false, err
	}

	// If we do not load a kernel into RAM, then we reboot back into the first kernel
	// partition; else we boot directly from RAM.
	// TODO(ZX-2069): Eventually, no such kernel should be present.
	hasRAMKernel := files[len(files)-1].name == kernelNetsvcName
	return hasRAMKernel, err
}

// ValidateBoard reads the board info from the target and validates that it matches boardName.
func ValidateBoard(ctx context.Context, t tftp.Client, boardName string) error {
	var r *bytes.Reader
	var err error
	// Attempt to read a file. If the server tells us we need to wait, then try
	// again as long as it keeps telling us this. ErrShouldWait implies the server
	// is still responding and will eventually be able to handle our request.
	logger.Infof(ctx, "attempting to read %s...\n", boardInfoNetsvcName)
	for {
		if ctx.Err() != nil {
			return nil
		}
		r, err = t.Read(ctx, boardInfoNetsvcName)
		switch err {
		case nil:
		case tftp.ErrShouldWait:
			// The target is busy, so let's sleep for a bit before
			// trying again, otherwise we'll be wasting cycles and
			// printing too often.
			logger.Infof(ctx, "target is busy, retrying in one second\n")
			time.Sleep(time.Second)
			continue
		default:
		}
		break
	}
	if err != nil {
		return fmt.Errorf("Unable to read the board info from [%s]: %w", boardInfoNetsvcName, err)
	}
	buf := make([]byte, r.Size())
	if _, err = r.Read(buf); err != nil {
		return fmt.Errorf("Unable to read the board info from [%s]: %w", boardInfoNetsvcName, err)
	}
	// Get the bytes before the first null byte.
	if index := bytes.IndexAny(buf, "\x00"); index >= 0 {
		buf = buf[:index]
	}
	targetBoardName := string(buf)
	if targetBoardName != boardName {
		return fmt.Errorf("Expected target to be [%s], but found target is [%s]", boardName, targetBoardName)
	}
	return nil
}

// Boot prepares and boots a device at the given IP address.
func Boot(ctx context.Context, t tftp.Client, imgs []Image, cmdlineArgs []string, signers []ssh.Signer) error {
	hasRAMKernel, err := transferImages(ctx, t, imgs, cmdlineArgs, signers)
	if err != nil {
		return err
	}

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
	idx, ok := transferOrder(name)
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
	return retry.Retry(ctx, retry.WithMaxAttempts(retry.NewConstantBackoff(time.Second), 21), func() error {
		for _, f := range files {
			// Attempt to send a file. If the server tells us we need to wait, then try
			// again as long as it keeps telling us this. ErrShouldWait implies the server
			// is still responding and will eventually be able to handle our request.
			logger.Infof(ctx, "attempting to send %s (%d)...\n", f.name, f.size)
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
					logger.Infof(ctx, "target is busy, retrying in one second\n")
					time.Sleep(time.Second)
					continue
				default:
					if skipOnTransferError(f.name) {
						logger.Infof(ctx, "failed to send %s; skipping and continuing: %v\n", f.name, err)
					} else {
						logger.Infof(ctx, "failed to send %s; starting from the top: %v\n", f.name, err)
						return err
					}
				}
				break
			}
			logger.Infof(ctx, "done\n")
		}
		return nil
	}, nil)
}

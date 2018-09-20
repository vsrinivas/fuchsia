// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package system_update_package

import (
	"bufio"
	"fmt"
	"io"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
	"strings"
	"sync"
	"time"

	"app/context"
	"fidl/fuchsia/amber"
	"syscall/zx"
	"syscall/zx/zxwait"
	"syslog/logger"
)

type Package struct {
	name   string
	merkle string
}

func ConnectToUpdateSrvc() (*amber.ControlInterface, error) {
	context := context.CreateFromStartupInfo()
	req, pxy, err := amber.NewControlInterfaceRequest()

	if err != nil {
		logger.Errorf("control interface could not be acquired: %s", err)
		return nil, err
	}

	context.ConnectToEnvService(req)
	return pxy, nil
}

func ParseRequirements(pkgSrc io.ReadCloser, imgSrc io.ReadCloser) ([]*Package, []string, error) {
	imgs := []string{}
	pkgs := []*Package{}

	rdr := bufio.NewReader(pkgSrc)
	for {
		l, err := rdr.ReadString('\n')
		s := strings.TrimSpace(l)
		if (err == nil || err == io.EOF) && len(s) > 0 {
			entry := strings.Split(s, "=")
			if len(entry) != 2 {
				return nil, nil, fmt.Errorf("parser: entry format %q", s)
			} else {
				pkgs = append(pkgs, &Package{name: entry[0], merkle: entry[1]})
			}
		}

		if err != nil {
			if err != io.EOF {
				return nil, nil, fmt.Errorf("parser: got error reading packages file %s", err)
			}
			break
		}
	}

	rdr = bufio.NewReader(imgSrc)

	for {
		l, err := rdr.ReadString('\n')
		s := strings.TrimSpace(l)
		if (err == nil || err == io.EOF) && len(s) > 0 {
			imgs = append(imgs, s)
		}

		if err != nil {
			if err != io.EOF {
				return nil, nil, fmt.Errorf("parser: got error reading images file %s", err)
			}
			break
		}
	}

	return pkgs, imgs, nil
}

func FetchPackages(pkgs []*Package, amber *amber.ControlInterface) error {
	workerWg := &sync.WaitGroup{}
	workerCount := runtime.NumCPU()
	workerWg.Add(workerCount)

	pkgChan := make(chan *Package, len(pkgs))
	errChan := make(chan error, len(pkgs))

	for i := 0; i < workerCount; i++ {
		go fetchWorker(pkgChan, amber, workerWg, errChan)
	}

	for _, pkg := range pkgs {
		pkgChan <- pkg
	}

	// stuffed in all the packages, close the channel
	close(pkgChan)

	// wait for the workers to finish
	workerWg.Wait()
	close(errChan)

	var errs []string

	for err := range errChan {
		errs = append(errs, err.Error())
	}

	if len(errs) > 0 {
		return fmt.Errorf("%d packages had errors: %s", len(errs), strings.Join(errs, ","))
	}

	return nil
}

func fetchWorker(c <-chan *Package, amber *amber.ControlInterface, done *sync.WaitGroup,
	e chan<- error) {
	defer done.Done()
	for p := range c {
		if err := fetchPackage(p, amber); err != nil {
			e <- err
		}
	}
}

func fetchPackage(p *Package, amber *amber.ControlInterface) error {
	logger.Infof("requesting %s/%s from update system", p.name, p.merkle)
	h, err := amber.GetUpdateComplete(p.name, nil, &p.merkle)
	if err != nil {
		return fmt.Errorf("fetch: failed submitting update request: %s", err)
	}
	defer h.Close()

	signals, err := zxwait.Wait(*h.Handle(),
		zx.SignalChannelPeerClosed|zx.SignalChannelReadable|zx.SignalUser0,
		zx.TimensecInfinite)
	if err != nil {
		return fmt.Errorf("fetch: error waiting on result channel: %s", err)
	}

	// we encountered a failure
	if signals&zx.SignalUser0 == zx.SignalUser0 {
		logger.Errorf("fetch: update service signaled failure getting %q, reading error", p.name)

		// if the channel isn't readable, do a bounded wait to try to get an error message from it
		if signals&zx.SignalChannelReadable != zx.SignalChannelReadable {
			signals, err = zxwait.Wait(*h.Handle(),
				zx.SignalChannelPeerClosed|zx.SignalChannelReadable,
				zx.Sys_deadline_after(zx.Duration((3 * time.Second).Nanoseconds())))
		}

		// if we can read, try it
		if err == nil && signals&zx.SignalChannelReadable == zx.SignalChannelReadable {
			buf := make([]byte, 1024)
			buf, err = readBytesFromHandle(&h, buf)
			if err == nil {
				return fmt.Errorf("fetch: update service failed getting %q: %s", p.name,
					string(buf))
			}
		}

		return fmt.Errorf("fetch: update service failed getting %q: %s", p.name, err)
	}

	if signals&zx.SignalChannelReadable == zx.SignalChannelReadable {
		buf := make([]byte, 128)
		buf, err := readBytesFromHandle(&h, buf)
		if err != nil {
			return fmt.Errorf("fetch: error reading channel %s", err)
		}
		logger.Infof("package %q installed at %q", p.name, string(buf))
	} else {
		return fmt.Errorf("fetch: reply channel was not readable")
	}
	return nil
}

// readBytesFromHandle attempts to read any available bytes from the channel. A buffer may be
// passed in to hold the bytes. If the buffer is too small or if the passed buffer is nil, an
// appropriately sized buffer will be allocated to hold the message. The function makes no attempt
// to validate the channel it is passed is actually readable.
func readBytesFromHandle(h *zx.Channel, buf []byte) ([]byte, error) {
	if buf == nil {
		buf = make([]byte, 1024)
	}
	i, _, err := h.Read(buf, []zx.Handle{}, 0)
	// possible retry if we need a larger buffer
	if err != nil {
		if zxErr, ok := err.(zx.Error); ok && zxErr.Status == zx.ErrBufferTooSmall {
			buf = make([]byte, i)
			i, _, err = h.Read(buf, []zx.Handle{}, 0)
		}
	}
	return buf[:i], err
}

var diskImagerPath = filepath.Join("/boot", "bin", "install-disk-image")

func WriteImgs(imgs []string, imgsPath string) error {
	logger.Infof("Writing images %+v from %q", imgs, imgsPath)

	for _, img := range imgs {
		imgPath := filepath.Join(imgsPath, img)
		if fi, err := os.Stat(imgPath); err != nil || fi.Size() == 0 {
			logger.Errorf("img_writer: %s image not found or zero length, skipping", img)
			continue
		}

		var c *exec.Cmd
		switch img {
		case "efi":
			c = exec.Command(diskImagerPath, "install-efi")
		case "kernc":
			c = exec.Command(diskImagerPath, "install-kernc")
		case "zbi", "zbi.signed":
			c = exec.Command(diskImagerPath, "install-zircona")
		case "zedboot", "zedboot.signed":
			c = exec.Command(diskImagerPath, "install-zirconr")
		case "bootloader":
			c = exec.Command(diskImagerPath, "install-bootloader")
		default:
			return fmt.Errorf("unrecognized image %q", img)
		}

		err := writeImg(c, imgPath)
		if err != nil {
			logger.Errorf("img_writer: error writing image: %s", err)
			return err
		}
		logger.Infof("img_writer: wrote %s successfully from %s", img, imgPath)
	}
	return nil
}

func writeImg(c *exec.Cmd, path string) error {
	info, err := os.Stat(path)
	if err != nil {
		return err
	}
	if info.Size() == 0 {
		return fmt.Errorf("img_writer: image file is empty!")
	}
	imgFile, err := os.Open(path)
	if err != nil {
		return err
	}

	defer imgFile.Close()
	c.Stdin = imgFile

	if err = c.Start(); err != nil {
		return fmt.Errorf("img_writer: error starting command: %s", err)
	}

	if err = c.Wait(); err != nil {
		return fmt.Errorf("img_writer: command failed during execution: %s", err)
	}

	return nil
}

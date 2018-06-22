// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package system_update_package

import (
	"bufio"
	"fmt"
	"io"
	"log"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
	"strings"
	"sync"

	"app/context"
	"fidl/fuchsia/amber"
	"syscall/zx"
	"syscall/zx/zxwait"
)

type Package struct {
	name   string
	merkle string
}

func ConnectToUpdateSrvc() (*amber.ControlInterface, error) {
	context := context.CreateFromStartupInfo()
	req, pxy, err := amber.NewControlInterfaceRequest()

	if err != nil {
		log.Println("control interface could not be acquired: %s", err)
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
	errCount := 0

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

	for range errChan {
		errCount++
	}

	if errCount > 0 {
		return fmt.Errorf("%d packages had errors", errCount)
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
	log.Printf("requesting %s/%s from update system", p.name, p.merkle)
	h, err := amber.GetUpdateComplete(p.name, nil, &p.merkle)
	if err != nil {
		return fmt.Errorf("fetch: failed submitting update request: %s", err)
	}
	defer h.Close()

	signals, err := zxwait.Wait(*h.Handle(), zx.SignalChannelPeerClosed|zx.SignalChannelReadable,
		zx.TimensecInfinite)
	if err != nil {
		return fmt.Errorf("fetch: error waiting on result channel: %s", err)
	}

	buf := make([]byte, 128)
	if signals&zx.SignalChannelReadable == zx.SignalChannelReadable {
		i, _, err := h.Read(buf, []zx.Handle{}, 0)
		if err != nil {
			return fmt.Errorf("fetch: error reading channel %s", err)
		}
		log.Printf("package %q installed at %q", p.name, string(buf[0:i]))
	} else {
		return fmt.Errorf("fetch: reply channel was not readable")
	}
	return nil
}

func WriteImgs(imgs []string, imgsPath string) error {
	for _, img := range imgs {
		var c *exec.Cmd
		switch img {
		case "efi":
			c = exec.Command("install-disk-image", "install-efi")
		case "kernc":
			c = exec.Command("install-disk-image", "install-kernc")
		default:
			return fmt.Errorf("unrecognized image %q", img)
		}

		err := writeImg(c, filepath.Join(imgsPath, img))
		if err != nil {
			return err
		}
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

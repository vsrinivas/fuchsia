// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package system_update_package

import (
	"bufio"
	"fmt"
	"io"
	"io/ioutil"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"syscall/zx"
	"syscall/zx/zxwait"

	"app/context"
	"fidl/fuchsia/amber"
	"syslog/logger"
)

type Package struct {
	namever string
	merkle  string
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
				pkgs = append(pkgs, &Package{namever: entry[0], merkle: entry[1]})
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
	var errCount int
	for _, pkg := range pkgs {
		if err := fetchPackage(pkg, amber); err != nil {
			logger.Errorf("fetch error: %s", err)
			errCount++
		}
	}

	if errCount > 0 {
		return fmt.Errorf("system update failed, %d packages had errors", errCount)
	}

	return nil
}

func fetchPackage(p *Package, amber *amber.ControlInterface) error {
	parts := strings.SplitN(p.namever, "/", 2)
	name, version := parts[0], parts[1]

	b, err := ioutil.ReadFile(filepath.Join("/pkgfs/packages", name, version, "meta"))
	if err == nil {
		// package is already installed, skip
		if string(b) == p.merkle {
			return nil
		}
	}

	logger.Infof("requesting %s/%s from update system", p.namever, p.merkle)
	ch, err := amber.GetUpdateComplete(name, &version, &p.merkle)
	if err != nil {
		return fmt.Errorf("fetch: GetUpdateComplete error: %s", err)
	}

	signals, err := zxwait.Wait(*ch.Handle(),
		zx.SignalChannelPeerClosed|zx.SignalChannelReadable,
		zx.TimensecInfinite)
	if err != nil {
		return fmt.Errorf("fetch: wait failure: %s", err)
	}

	if signals&zx.SignalChannelReadable != 0 {
		var buf [64 * 1024]byte
		n, _, err := ch.Read(buf[:], []zx.Handle{}, 0)
		if err != nil {
			return fmt.Errorf("fetch: error reading channel %s", err)
		}
		if signals&zx.SignalUser0 != 0 {
			return fmt.Errorf("fetch: error from daemon: %s", buf[:n])
		}
		logger.Infof("package %q installed at %q", p.namever, string(buf[:n]))
	} else {
		return fmt.Errorf("fetch: channel closed prematurely")
	}

	return nil
}

var diskImagerPath = filepath.Join("/boot", "bin", "install-disk-image")

func WriteImgs(imgs []string, imgsPath string) error {
	logger.Infof("Writing images %+v from %q", imgs, imgsPath)

	for _, img := range imgs {
		imgPath := filepath.Join(imgsPath, img)
		if fi, err := os.Stat(imgPath); err != nil || fi.Size() == 0 {
			logger.Errorf("img_writer: %q image not found or zero length, skipping", img)
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
			// TODO(ZX-2689): remove once the bootloader is booting zirconr as recovery.
			if img == "zedboot.signed" {
				c = exec.Command(diskImagerPath, "install-zirconb")
			}
		case "bootloader":
			c = exec.Command(diskImagerPath, "install-bootloader")
		default:
			return fmt.Errorf("unrecognized image %q", img)
		}

		logger.Infof("img_writer: writing %q from %q", img, imgPath)
		out, err := writeImg(c, imgPath)
		if len(out) != 0 {
			logger.Infof("img_writer: %s", string(out))
		}
		if err != nil {
			logger.Errorf("img_writer: error writing %q from %q: %s", img, imgPath, err)
			if len(out) != 0 {
				logger.Errorf("img_writer: %s", string(out))
			}
			return err
		}
		logger.Infof("img_writer: wrote %q successfully from %q", img, imgPath)
	}
	return nil
}

func writeImg(c *exec.Cmd, path string) ([]byte, error) {
	info, err := os.Stat(path)
	if err != nil {
		return nil, err
	}
	if info.Size() == 0 {
		return nil, fmt.Errorf("img_writer: image file is empty!")
	}
	imgFile, err := os.Open(path)
	if err != nil {
		return nil, err
	}

	defer imgFile.Close()
	c.Stdin = imgFile

	return c.CombinedOutput()
}

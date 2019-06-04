// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package system_updater

import (
	"bufio"
	"bytes"
	"fmt"
	"io"
	"io/ioutil"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"syscall/zx"

	"app/context"
	fuchsiaio "fidl/fuchsia/io"
	"fidl/fuchsia/pkg"
	"syslog"
)

type Package struct {
	namever string
	merkle  string
}

func ConnectToPackageResolver() (*pkg.PackageResolverInterface, error) {
	context := context.CreateFromStartupInfo()
	req, pxy, err := pkg.NewPackageResolverInterfaceRequest()

	if err != nil {
		syslog.Errorf("control interface could not be acquired: %s", err)
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

func FetchPackages(pkgs []*Package, resolver *pkg.PackageResolverInterface) error {
	var errCount int
	for _, pkg := range pkgs {
		if err := fetchPackage(pkg, resolver); err != nil {
			syslog.Errorf("fetch error: %s", err)
			errCount++
		}
	}

	if errCount > 0 {
		return fmt.Errorf("system update failed, %d packages had errors", errCount)
	}

	return nil
}

func fetchPackage(p *Package, resolver *pkg.PackageResolverInterface) error {
	b, err := ioutil.ReadFile(filepath.Join("/pkgfs/packages", p.namever, "meta"))
	if err == nil {
		// package is already installed, skip
		if string(b) == p.merkle {
			return nil
		}
	}

	pkgUri := fmt.Sprintf("fuchsia-pkg://fuchsia.com/%s?hash=%s", p.namever, p.merkle)
	selectors := []string{}
	updatePolicy := pkg.UpdatePolicy{}
	dirReq, dirPxy, err := fuchsiaio.NewDirectoryInterfaceRequest()
	defer dirPxy.Close()

	syslog.Infof("requesting %s from update system", pkgUri)

	status, err := resolver.Resolve(pkgUri, selectors, updatePolicy, dirReq)
	if err != nil {
		return fmt.Errorf("fetch: Resolve error: %s", err)
	}

	statusErr := zx.Status(status)
	if statusErr != zx.ErrOk {
		return fmt.Errorf("fetch: Resolve status: %s", statusErr)
	}

	return nil
}

var diskImagerPath = filepath.Join("/pkg", "bin", "install-disk-image")

func ValidateImgs(imgs []string, imgsPath string) error {
	boardPath := filepath.Join(imgsPath, "board")
	actual, err := ioutil.ReadFile(boardPath)
	if err != nil {
		if os.IsNotExist(err) {
			return nil
		} else {
			return err
		}
	}
	expected, err := ioutil.ReadFile("/config/build-info/board")
	if err != nil {
		return err
	}
	if !bytes.Equal(actual, expected) {
		return fmt.Errorf("parser: expected board name %s found %s", expected, actual)
	}
	return nil
}

func WriteImgs(imgs []string, imgsPath string) error {
	syslog.Infof("Writing images %+v from %q", imgs, imgsPath)

	for _, img := range imgs {
		imgPath := filepath.Join(imgsPath, img)
		if fi, err := os.Stat(imgPath); err != nil || fi.Size() == 0 {
			syslog.Warnf("img_writer: %q image not found or zero length, skipping", img)
			continue
		}

		var c *exec.Cmd
		switch img {
		case "zbi", "zbi.signed":
			c = exec.Command(diskImagerPath, "install-zircona")
		case "zedboot", "zedboot.signed":
			c = exec.Command(diskImagerPath, "install-zirconr")
		case "bootloader":
			c = exec.Command(diskImagerPath, "install-bootloader")
		case "board":
			continue
		default:
			return fmt.Errorf("unrecognized image %q", img)
		}

		syslog.Infof("img_writer: writing %q from %q", img, imgPath)
		out, err := writeImg(c, imgPath)
		if len(out) != 0 {
			syslog.Infof("img_writer: %s", string(out))
		}
		if err != nil {
			syslog.Errorf("img_writer: error writing %q from %q: %s", img, imgPath, err)
			if len(out) != 0 {
				syslog.Errorf("img_writer: %s", string(out))
			}
			return err
		}
		syslog.Infof("img_writer: wrote %q successfully from %q", img, imgPath)
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

// UpdateCurrentChannel persists the update channel info for a successful update
func UpdateCurrentChannel() error {
	targetPath := "/misc/ota/target_channel.json"
	contents, err := ioutil.ReadFile(targetPath)
	if err != nil {
		return fmt.Errorf("no target channel recorded in %v: %v", targetPath, err)
	}
	currentPath := "/misc/ota/current_channel.json"
	partPath := currentPath + ".part"
	f, err := os.Create(partPath)
	if err != nil {
		return fmt.Errorf("unable to write current channel to %v: %v", partPath, err)
	}
	defer f.Close()
	buf := bytes.NewBuffer(contents)
	_, err = buf.WriteTo(f)
	if err != nil {
		return fmt.Errorf("unable to write current channel to %v: %v", currentPath, err)
	}
	f.Sync()
	f.Close()
	if err := os.Rename(partPath, currentPath); err != nil {
		return fmt.Errorf("error moving %v to %v: %v", partPath, currentPath, err)
	}
	return nil
}

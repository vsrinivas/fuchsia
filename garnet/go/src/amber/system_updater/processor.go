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
	"path/filepath"
	"strings"
	"syscall"
	"syscall/zx"
	"syscall/zx/fdio"
	"syscall/zx/fidl"
	zxio "syscall/zx/io"

	"app/context"
	fuchsiaio "fidl/fuchsia/io"
	"fidl/fuchsia/mem"
	"fidl/fuchsia/paver"
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

func ConnectToPaver() (*paver.DataSinkInterface, error) {
	context := context.CreateFromStartupInfo()
	req, pxy, err := paver.NewPaverInterfaceRequest()

	if err != nil {
		syslog.Errorf("control interface could not be acquired: %s", err)
		return nil, err
	}
	defer pxy.Close();

	context.ConnectToEnvService(req)

	req2, pxy2, err := paver.NewDataSinkInterfaceRequest()
	if err != nil {
		syslog.Errorf("control interface could not be acquired: %s", err)
		return nil, err
	}

	err = pxy.FindDataSink(req2)
	if err != nil {
		return nil, err
	}
	return pxy2, nil
}

// CacheUpdatePackage caches the requested, possibly merkle-pinned, update
// package URL and returns the pkgfs path to the package.
func CacheUpdatePackage(updateURL string, resolver *pkg.PackageResolverInterface) (string, error) {
	dirPxy, err := resolvePackage(updateURL, resolver)
	if err != nil {
		return "", err
	}
	defer dirPxy.Close()

	channelProxy := (*fidl.ChannelProxy)(dirPxy)
	updateDir := fdio.Directory{fdio.Node{(*zxio.NodeInterface)(channelProxy)}}
	path := "meta"
	f, err := updateDir.Open(path, zxio.OpenRightReadable, zxio.ModeTypeFile)
	if err != nil {
		return "", err
	}
	file := os.NewFile(uintptr(syscall.OpenFDIO(f)), path)
	defer file.Close()
	b, err := ioutil.ReadAll(file)
	if err != nil {
		return "", err
	}
	merkle := string(b)
	return "/pkgfs/versions/" + merkle, nil
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
	b, err := ioutil.ReadFile(filepath.Join("/pkgfs/versions", p.merkle, "meta"))
	if err == nil {
		// package is already installed, skip
		if string(b) == p.merkle {
			return nil
		}
	}

	pkgURL := fmt.Sprintf("fuchsia-pkg://fuchsia.com/%s?hash=%s", p.namever, p.merkle)
	dirPxy, err := resolvePackage(pkgURL, resolver)
	if dirPxy != nil {
		dirPxy.Close()
	}
	return err
}

func resolvePackage(pkgURL string, resolver *pkg.PackageResolverInterface) (*fuchsiaio.DirectoryInterface, error) {
	selectors := []string{}
	updatePolicy := pkg.UpdatePolicy{}
	dirReq, dirPxy, err := fuchsiaio.NewDirectoryInterfaceRequest()
	if err != nil {
		return nil, err
	}

	syslog.Infof("requesting %s from update system", pkgURL)

	status, err := resolver.Resolve(pkgURL, selectors, updatePolicy, dirReq)
	if err != nil {
		dirPxy.Close()
		return nil, fmt.Errorf("fetch: Resolve error: %s", err)
	}

	statusErr := zx.Status(status)
	if statusErr != zx.ErrOk {
		dirPxy.Close()
		return nil, fmt.Errorf("fetch: Resolve status: %s", statusErr)
	}

	return dirPxy, nil
}

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

func WriteImgs(svc *paver.DataSinkInterface, imgs []string, imgsPath string) error {
	syslog.Infof("Writing images %+v from %q", imgs, imgsPath)

	for _, img := range imgs {
		if err := writeImg(svc, img, imgsPath); err != nil {
			return err
		}
	}
	return nil
}

func writeAsset(svc *paver.DataSinkInterface, configuration paver.Configuration, asset paver.Asset, payload *mem.Buffer) error {
	status, err := svc.WriteAsset(configuration, asset, *payload)
	if err != nil {
		return err
	}
	statusErr := zx.Status(status)
	if statusErr != zx.ErrOk {
		return &zx.Error{Status: statusErr}
	}
	return nil
}

func writeImg(svc *paver.DataSinkInterface, img string, imgsPath string) error {
	imgPath := filepath.Join(imgsPath, img)

	f, err := os.Open(imgPath)
	if err != nil {
		syslog.Warnf("img_writer: %q image not found, skipping", img)
		return nil
	}
	if fi, err := f.Stat(); err != nil || fi.Size() == 0 {
		syslog.Warnf("img_writer: %q zero length, skipping", img)
		return nil
	}
	defer f.Close()

	buffer, err := bufferForFile(f)
	if err != nil {
		return fmt.Errorf("img_writer: while getting vmo for %q: %q", img, err)
	}
	defer buffer.Vmo.Close()

	var writeImg func() error
	switch img {
	case "zbi", "zbi.signed":
		childVmo, err := buffer.Vmo.CreateChild(zx.VMOChildOptionCopyOnWrite|zx.VMOChildOptionResizable, 0, buffer.Size)
		if err != nil {
			return fmt.Errorf("img_writer: while getting vmo for %q: %q", img, err)
		}
		buffer2 := &mem.Buffer{
			Vmo:  childVmo,
			Size: buffer.Size,
		}
		defer buffer2.Vmo.Close()
		writeImg = func() error {
			if err := writeAsset(svc, paver.ConfigurationA, paver.AssetKernel, buffer); err != nil {
				return err
			}
			if err := writeAsset(svc, paver.ConfigurationB, paver.AssetKernel, buffer2); err != nil {
				asZxErr, ok := err.(*zx.Error)
				if ok && asZxErr.Status == zx.ErrNotSupported {
					syslog.Warnf("img_writer: skipping writing %q to B: %v", img, err)
				} else {
					return err
				}
			}
			return nil
		}
	case "fuchsia.vbmeta":
		childVmo, err := buffer.Vmo.CreateChild(zx.VMOChildOptionCopyOnWrite|zx.VMOChildOptionResizable, 0, buffer.Size)
		if err != nil {
			return fmt.Errorf("img_writer: while getting vmo for %q: %q", img, err)
		}
		buffer2 := &mem.Buffer{
			Vmo:  childVmo,
			Size: buffer.Size,
		}
		defer buffer2.Vmo.Close()
		writeImg = func() error {
			if err := writeAsset(svc, paver.ConfigurationA, paver.AssetVerifiedBootMetadata, buffer); err != nil {
				return err
			}
			return writeAsset(svc, paver.ConfigurationB, paver.AssetVerifiedBootMetadata, buffer2)
		}
	case "zedboot", "zedboot.signed":
		writeImg = func() error {
			return writeAsset(svc, paver.ConfigurationRecovery, paver.AssetKernel, buffer)
		}
	case "recovery.vbmeta":
		writeImg = func() error {
			return writeAsset(svc, paver.ConfigurationRecovery, paver.AssetVerifiedBootMetadata, buffer)
		}
	case "bootloader":
		writeImg = func() error {
			status, err := svc.WriteBootloader(*buffer)
			if err != nil {
				return err
			}
			statusErr := zx.Status(status)
			if statusErr != zx.ErrOk {
				return fmt.Errorf("%s", statusErr)
			}
			return nil
		}
	case "board":
		return nil
	default:
		return fmt.Errorf("unrecognized image %q", img)
	}

	syslog.Infof("img_writer: writing %q from %q", img, imgPath)
	if err := writeImg(); err != nil {
		return fmt.Errorf("img_writer: error writing %q: %q", img, err)
	}
	syslog.Infof("img_writer: wrote %q successfully from %q", img, imgPath)

	return nil
}

func bufferForFile(f *os.File) (*mem.Buffer, error) {
	fio := syscall.FDIOForFD(int(f.Fd())).(*fdio.File)
	if fio == nil {
		return nil, fmt.Errorf("not fdio file")
	}

	status, buffer, err := fio.FileInterface().GetBuffer(zxio.VmoFlagRead)
	if err != nil {
		return nil, fmt.Errorf("GetBuffer fidl error: %q", err)
	}
	statusErr := zx.Status(status)
	if statusErr != zx.ErrOk {
		return nil, fmt.Errorf("GetBuffer error: %q", statusErr)
	}
	defer buffer.Vmo.Close()

	// VMOs acquired over FIDL are not guaranteed to be resizable, so create a child VMO that is.
	childVmo, err := buffer.Vmo.CreateChild(zx.VMOChildOptionCopyOnWrite|zx.VMOChildOptionResizable, 0, buffer.Size)
	if err != nil {
		return nil, err
	}

	return &mem.Buffer{
		Vmo:  childVmo,
		Size: buffer.Size,
	}, nil
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

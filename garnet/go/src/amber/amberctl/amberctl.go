// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package amberctl

import (
	"bytes"
	"crypto/sha256"
	"encoding/hex"
	"encoding/json"
	"flag"
	"fmt"
	"io"
	"io/ioutil"
	"log"
	"net/http"
	"net/url"
	"os"
	"path/filepath"
	"strings"
	"syscall/zx"
	"time"
	"unicode"

	"amber/urlscope"
	"app/context"
	"fidl/fuchsia/amber"
	fuchsiaio "fidl/fuchsia/io"
	"fidl/fuchsia/pkg"
)

const usage = `usage: %s <command> [opts]
Commands
    get_up        - get an update for a package
      Options
        -n:      name of the package
        -v:      version of the package to retrieve, if none is supplied any
                 package instance could match
        -m:      merkle root of the package to retrieve, if none is supplied
                 any package instance could match

    get_blob      - get the specified content blob
        -i: content ID of the blob

    add_src       - add a source to the list we can use
        -n: name of the update source (optional, with URL)
        -f: file path or url to a source config file
        -h: SHA256 hash of source config file (optional, with URL)
        -x: do not disable other active sources (if the provided source is enabled)

    rm_src        - remove a source, if it exists
        -n: name of the update source

    list_srcs     - list the set of sources we can use

    enable_src
        -n: name of the update source
        -x: do not disable other active sources

    disable_src
        -n: name of the update source

    system_update - check for, download, and apply any available system update

    gc - trigger a garbage collection

    print_state - print go routine state of amber process
`

var (
	fs           = flag.NewFlagSet("default", flag.ExitOnError)
	pkgFile      = fs.String("f", "", "Path to a source config file")
	hash         = fs.String("h", "", "SHA256 hash of source config file (required if -f is a URL, ignored otherwise)")
	name         = fs.String("n", "", "Name of a source or package")
	version      = fs.String("v", "", "Version of a package")
	blobID       = fs.String("i", "", "Content ID of the blob")
	merkle       = fs.String("m", "", "Merkle root of the desired update.")
	nonExclusive = fs.Bool("x", false, "When adding or enabling a source, do not disable other sources.")
)

type ErrGetFile string

func NewErrGetFile(str string, inner error) ErrGetFile {
	return ErrGetFile(fmt.Sprintf("%s: %v", str, inner))
}

func (e ErrGetFile) Error() string {
	return string(e)
}

func doTest(pxy *amber.ControlInterface) error {
	v := int32(42)
	resp, err := pxy.DoTest(v)
	if err != nil {
		fmt.Println(err)
		return err
	}

	fmt.Printf("Response: %s\n", resp)
	return nil
}

func connectToAmber(ctx *context.Context) (*amber.ControlInterface, amber.ControlInterfaceRequest) {
	req, pxy, err := amber.NewControlInterfaceRequest()
	if err != nil {
		panic(err)
	}
	ctx.ConnectToEnvService(req)
	return pxy, req
}

func connectToPackageResolver(ctx *context.Context) (*pkg.PackageResolverInterface, pkg.PackageResolverInterfaceRequest) {
	req, pxy, err := pkg.NewPackageResolverInterfaceRequest()
	if err != nil {
		panic(err)
	}
	ctx.ConnectToEnvService(req)
	return pxy, req
}

func addSource(a *amber.ControlInterface) error {
	var cfg amber.SourceConfig

	if len(*pkgFile) == 0 {
		return fmt.Errorf("a url or file path (via -f) are required")
	}

	var source io.Reader
	url, err := url.Parse(*pkgFile)
	isURL := false
	if err == nil && url.IsAbs() {
		isURL = true
		var expectedHash []byte
		hash := strings.TrimSpace(*hash)
		if len(hash) != 0 {

			var err error
			expectedHash, err = hex.DecodeString(hash)
			if err != nil {
				return fmt.Errorf("hash is not a hex encoded string: %v", err)
			}
		}

		resp, err := http.Get(*pkgFile)
		if err != nil {
			return NewErrGetFile("failed to GET file", err)
		}
		defer resp.Body.Close()
		if resp.StatusCode != 200 {
			io.Copy(ioutil.Discard, resp.Body)
			return fmt.Errorf("GET response: %v", resp.Status)
		}

		body, err := ioutil.ReadAll(resp.Body)
		if err != nil {
			return fmt.Errorf("failed to read file body: %v", err)
		}

		if len(expectedHash) != 0 {
			hasher := sha256.New()
			hasher.Write(body)
			actualHash := hasher.Sum(nil)

			if !bytes.Equal(expectedHash, actualHash) {
				return fmt.Errorf("hash of config file does not match!")
			}
		}

		source = bytes.NewReader(body)

	} else {
		f, err := os.Open(*pkgFile)
		if err != nil {
			return fmt.Errorf("failed to open file: %v", err)
		}
		defer f.Close()

		source = f
	}

	if err := json.NewDecoder(source).Decode(&cfg); err != nil {
		return fmt.Errorf("failed to parse source config: %v", err)
	}

	if *name != "" {
		cfg.Id = *name
	}

	// Update the host segment of the URL with the original if it appears to have
	// only been de-scoped, so that link-local configurations retain ipv6 scopes.
	if isURL {
		if remote, err := url.Parse(cfg.RepoUrl); err == nil {
			if u := urlscope.Rescope(url, remote); u != nil {
				cfg.RepoUrl = u.String()
			}
		}
		if remote, err := url.Parse(cfg.BlobRepoUrl); err == nil {
			if u := urlscope.Rescope(url, remote); u != nil {
				cfg.BlobRepoUrl = u.String()
			}
		}
	}

	if cfg.BlobRepoUrl == "" {
		cfg.BlobRepoUrl = filepath.Join(cfg.RepoUrl, "blobs")
	}

	added, err := a.AddSrc(cfg)
	if err != nil {
		return fmt.Errorf("IPC encountered an error: %s", err)
	}
	if !added {
		return fmt.Errorf("request arguments properly formatted, but possibly otherwise invalid")
	}

	if isSourceConfigEnabled(&cfg) && !*nonExclusive {
		if err := disableAllSources(a, cfg.Id); err != nil {
			return err
		}
	}

	return nil
}

func rmSource(a *amber.ControlInterface) error {
	name := strings.TrimSpace(*name)
	if name == "" {
		return fmt.Errorf("no source id provided")
	}

	status, err := a.RemoveSrc(name)
	if err != nil {
		return fmt.Errorf("IPC encountered an error: %s", err)
	}
	switch status {
	case amber.StatusOk:
		return nil
	case amber.StatusErrNotFound:
		return fmt.Errorf("Source not found")
	case amber.StatusErr:
		return fmt.Errorf("Unspecified error")
	default:
		return fmt.Errorf("Unexpected status: %v", status)
	}
}

func getUp(r *pkg.PackageResolverInterface) error {
	if *name == "" {
		return fmt.Errorf("no source id provided")
	}

	var err error
	for i := 0; i < 3; i++ {
		err = getUpdateComplete(r, *name, version, merkle)
		if err == nil {
			break
		}
		fmt.Printf("Update failed with error %s, retrying...\n", err)
		time.Sleep(2 * time.Second)
	}
	return err
}

func listSources(a *amber.ControlInterface) error {
	srcs, err := a.ListSrcs()
	if err != nil {
		fmt.Printf("failed to list sources: %s\n", err)
		return err
	}

	for _, src := range srcs {
		encoder := json.NewEncoder(os.Stdout)
		encoder.SetIndent("", "    ")
		if err := encoder.Encode(src); err != nil {
			fmt.Printf("failed to encode source into json: %s\n", err)
			return err
		}
	}

	return nil
}

func setSourceEnablement(a *amber.ControlInterface, id string, enabled bool) error {
	status, err := a.SetSrcEnabled(id, enabled)
	if err != nil {
		return fmt.Errorf("call failure attempting to change source status: %s", err)
	}
	if status != amber.StatusOk {
		return fmt.Errorf("failure changing source status")
	}

	return nil
}

func isSourceConfigEnabled(cfg *amber.SourceConfig) bool {
	if cfg.StatusConfig == nil {
		return true
	}
	return cfg.StatusConfig.Enabled
}

func disableAllSources(a *amber.ControlInterface, except string) error {
	errorIds := []string{}
	cfgs, err := a.ListSrcs()
	if err != nil {
		return err
	}
	for _, cfg := range cfgs {
		if cfg.Id != except && isSourceConfigEnabled(&cfg) {
			if err := setSourceEnablement(a, cfg.Id, false); err != nil {
				log.Printf("error disabling %q: %s", cfg.Id, err)
				errorIds = append(errorIds, fmt.Sprintf("%q", cfg.Id))
			} else {
				fmt.Printf("Source %q disabled\n", cfg.Id)
			}
		}
	}
	if len(errorIds) > 0 {
		return fmt.Errorf("could not disable %s", strings.Join(errorIds, ", "))
	}
	return nil
}

func do(amberProxy *amber.ControlInterface, resolverProxy *pkg.PackageResolverInterface) int {
	switch os.Args[1] {
	case "get_up":
		if err := getUp(resolverProxy); err != nil {
			log.Printf("error getting an update: %s", err)
			return 1
		}
	case "get_blob":
		if *blobID == "" {
			log.Printf("no blob id provided")
			return 1
		}
		if err := amberProxy.GetBlob(*blobID); err != nil {
			log.Printf("error requesting blob fetch: %s", err)
			return 1
		}
	case "add_src":
		if err := addSource(amberProxy); err != nil {
			log.Printf("error adding source: %s", err)
			if _, ok := err.(ErrGetFile); ok {
				return 2
			} else {
				return 1
			}
		}
	case "rm_src":
		if err := rmSource(amberProxy); err != nil {
			log.Printf("error removing source: %s", err)
			return 1
		}
	case "list_srcs":
		if err := listSources(amberProxy); err != nil {
			log.Printf("error listing sources: %s", err)
			return 1
		}
	case "check":
		log.Printf("%q not yet supported\n", os.Args[1])
		return 1
	case "test":
		if err := doTest(amberProxy); err != nil {
			log.Printf("error testing connection to amber: %s", err)
			return 1
		}
	case "system_update":
		configured, err := amberProxy.CheckForSystemUpdate()
		if err != nil {
			log.Printf("error checking for system update: %s", err)
			return 1
		}

		if configured {
			fmt.Printf("triggered a system update check\n")
		} else {
			fmt.Printf("system update is not configured\n")
		}
	case "login":
		device, err := amberProxy.Login(*name)
		if err != nil {
			log.Printf("failed to login: %s", err)
			return 1
		}
		fmt.Printf("On your computer go to:\n\n\t%v\n\nand enter\n\n\t%v\n\n", device.VerificationUrl, device.UserCode)
	case "enable_src":
		if *name == "" {
			log.Printf("Error enabling source: no source id provided")
			return 1
		}
		err := setSourceEnablement(amberProxy, *name, true)
		if err != nil {
			log.Printf("Error enabling source: %s", err)
			return 1
		}
		fmt.Printf("Source %q enabled\n", *name)
		if !*nonExclusive {
			if err := disableAllSources(amberProxy, *name); err != nil {
				log.Printf("Error disabling sources: %s", err)
				return 1
			}
		}
	case "disable_src":
		if *name == "" {
			log.Printf("Error disabling source: no source id provided")
			return 1
		}
		err := setSourceEnablement(amberProxy, *name, false)
		if err != nil {
			log.Printf("Error disabling source: %s", err)
			return 1
		}
		fmt.Printf("Source %q disabled\n", *name)
	case "gc":
		err := amberProxy.Gc()
		if err != nil {
			log.Printf("Error collecting garbage: %s", err)
			return 1
		}
		log.Printf("Started garbage collection. See logs for details")
	case "print_state":
		if err := filepath.Walk("/hub", func(path string, info os.FileInfo, err error) error {
			if err != nil {
				return err
			}
			switch name := info.Name(); name {
			case "goroutines":
				if f, err := os.Open(path); err != nil {
					return err
				} else {
					_, err := io.Copy(os.Stdout, f)
					return err
				}
			case "hub", "c", "r", "amber.cmx", "out", "debug":
				return nil
			default:
				if info.IsDir() {
					for _, r := range name {
						if !unicode.IsDigit(r) {
							return filepath.SkipDir
						}
					}
				}
				return nil
			}
		}); err != nil {
			log.Printf("Error printing process state: %s", err)
			return 1
		}
	default:

		fmt.Printf("Error, %q is not a recognized command\n", os.Args[1])
		fmt.Printf(usage, filepath.Base(os.Args[0]))
		return -1
	}

	return 0
}

func Main() {
	if len(os.Args) < 2 {
		fmt.Println("Error: no command provided")
		fmt.Printf(usage, filepath.Base(os.Args[0]))
		os.Exit(-1)
	}

	fs.Parse(os.Args[2:])

	ctx := context.CreateFromStartupInfo()

	amberProxy, _ := connectToAmber(ctx)
	defer amberProxy.Close()

	resolverProxy, _ := connectToPackageResolver(ctx)
	defer resolverProxy.Close()

	os.Exit(do(amberProxy, resolverProxy))
}

type ErrDaemon string

func NewErrDaemon(str string) ErrDaemon {
	return ErrDaemon(fmt.Sprintf("amberctl: daemon error: %s", str))
}

func (e ErrDaemon) Error() string {
	return string(e)
}

type resolveResult struct {
	status zx.Status
	err    error
}

func getUpdateComplete(r *pkg.PackageResolverInterface, name string, version *string, merkle *string) error {
	pkgUri := fmt.Sprintf("fuchsia-pkg://fuchsia.com/%s", name)
	if *version != "" {
		pkgUri = fmt.Sprintf("%s/%s", pkgUri, *version)
	}
	if *merkle != "" {
		pkgUri = fmt.Sprintf("%s?hash=%s", pkgUri, *merkle)
	}

	selectors := []string{}
	updatePolicy := pkg.UpdatePolicy{}

	dirReq, dirPxy, err := fuchsiaio.NewDirectoryInterfaceRequest()
	if err != nil {
		return err
	}
	defer dirPxy.Close()

	ch := make(chan resolveResult)
	go func() {
		status, err := r.Resolve(pkgUri, selectors, updatePolicy, dirReq)
		ch <- resolveResult{
			status: zx.Status(status),
			err:    err,
		}
	}()

	for {
		select {
		case result := <-ch:
			if result.err != nil {
				return fmt.Errorf("error getting update: %s", result.err)
			}
			if result.status != zx.ErrOk {
				return fmt.Errorf("fetch: Resolve status: %s", result.status)
			}
			return nil
		case <-time.After(3 * time.Second):
			log.Println("Awaiting response...")
		}
	}
}

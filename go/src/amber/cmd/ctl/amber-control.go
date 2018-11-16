// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

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
	"syscall/zx/zxwait"
	"time"

	"app/context"
	"fidl/fuchsia/amber"
)

const usage = `usage: amber_ctl <command> [opts]
Commands
    get_up        - get an update for a package
      Options
        -n:      name of the package
        -v:      version of the package to retrieve, if none is supplied any
                 package instance could match
        -m:      merkle root of the package to retrieve, if none is supplied
                 any package instance could match
        -nowait: exit once package installation has started, but don't wait for
                 package activation

    get_blob      - get the specified content blob
        -i: content ID of the blob

    add_src       - add a source to the list we can use
        -n: name of the update source (optional, with URL)
        -f: file path or url to a source config file
        -h: SHA256 hash of source config file (optional, with URL)

    rm_src        - remove a source, if it exists
        -n: name of the update source

    list_srcs     - list the set of sources we can use

    enable_src
        -n: name of the update source

    disable_src
        -n: name of the update source

    system_update - check for, download, and apply any available system update

    gc - trigger a garbage collection
`

var (
	fs      = flag.NewFlagSet("default", flag.ExitOnError)
	pkgFile = fs.String("f", "", "Path to a source config file")
	hash    = fs.String("h", "", "SHA256 hash of source config file (required if -f is a URL, ignored otherwise)")
	name    = fs.String("n", "", "Name of a source or package")
	version = fs.String("v", "", "Version of a package")
	blobID  = fs.String("i", "", "Content ID of the blob")
	noWait  = fs.Bool("nowait", false, "Return once installation has started, package will not yet be available.")
	merkle  = fs.String("m", "", "Merkle root of the desired update.")
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

func connect(ctx *context.Context) (*amber.ControlInterface, amber.ControlInterfaceRequest) {
	req, pxy, err := amber.NewControlInterfaceRequest()
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
	if err == nil && url.IsAbs() {
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

	if len(*name) != 0 {
		cfg.Id = *name
	}

	if cfg.BlobRepoUrl == "" {
		cfg.BlobRepoUrl = filepath.Join(cfg.RepoUrl, "blobs")
	}

	added, err := a.AddSrc(cfg)
	if !added {
		return fmt.Errorf("request arguments properly formatted, but possibly otherwise invalid")
	}
	if err != nil {
		return fmt.Errorf("IPC encountered an error: %s", err)
	}

	return nil
}

func rmSource(a *amber.ControlInterface) error {
	name := strings.TrimSpace(*name)
	if len(name) == 0 {
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

func getUp(a *amber.ControlInterface) error {
	if *noWait {
		c, err := a.GetUpdateComplete(*name, version, merkle)
		if err != nil {
			return fmt.Errorf("Error getting update %s\n", err)
		}
		c.Close()

		fmt.Printf("Update requested %s\n", *name)
		return nil
	}

	var err error
	for i := 0; i < 3; i++ {
		err = getUpdateComplete(a, *name, version, merkle)
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
		fmt.Printf("failed to list sources: %s", err)
		return err
	}

	for _, src := range srcs {
		encoder := json.NewEncoder(os.Stdout)
		encoder.SetIndent("", "    ")
		if err := encoder.Encode(src); err != nil {
			fmt.Printf("failed to encode source into json: %s", err)
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

func do(proxy *amber.ControlInterface) int {
	switch os.Args[1] {
	case "get_up":
		if err := getUp(proxy); err != nil {
			log.Printf("error getting an update: %s", err)
			return 1
		}
	case "get_blob":
		if err := proxy.GetBlob(*blobID); err != nil {
			log.Printf("error requesting blob fetch: %s", err)
			return 1
		}
	case "add_src":
		if err := addSource(proxy); err != nil {
			log.Printf("error adding source: %s", err)
			if _, ok := err.(ErrGetFile); ok {
				return 2
			} else {
				return 1
			}
		}
	case "rm_src":
		if err := rmSource(proxy); err != nil {
			log.Printf("error removing source: %s", err)
			return 1
		}
	case "list_srcs":
		if err := listSources(proxy); err != nil {
			log.Printf("error listing sources: %s", err)
			return 1
		}
	case "check":
		log.Printf("%q not yet supported\n", os.Args[1])
		return 1
	case "test":
		if err := doTest(proxy); err != nil {
			log.Printf("error testing connection to amber: %s", err)
			return 1
		}
	case "system_update":
		configured, err := proxy.CheckForSystemUpdate()
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
		device, err := proxy.Login(*name)
		if err != nil {
			log.Printf("failed to login: %s", err)
			return 1
		}
		fmt.Printf("On your computer go to:\n\n\t%v\n\nand enter\n\n\t%v\n\n", device.VerificationUrl, device.UserCode)
	case "enable_src":
		err := setSourceEnablement(proxy, *name, true)
		if err != nil {
			fmt.Printf("Error enabling source: %s", err)
			return 1
		}
		fmt.Printf("Source %q enabled\n", *name)
	case "disable_src":
		err := setSourceEnablement(proxy, *name, false)
		if err != nil {
			fmt.Printf("Error disabling source: %s", err)
			return 1
		}
		fmt.Printf("Source %q disabled\n", *name)
	case "gc":
		err := proxy.Gc()
		if err != nil {
			log.Printf("Error collecting garbage: %s", err)
			return 1
		}
		log.Printf("Started garbage collection. See logs for details")
	default:

		log.Printf("Error, %q is not a recognized command\n%s",
			os.Args[1], usage)
		return -1
	}

	return 0
}

func main() {
	if len(os.Args) < 2 {
		fmt.Printf("Error: no command provided\n%s", usage)
		os.Exit(-1)
	}

	fs.Parse(os.Args[2:])

	proxy, _ := connect(context.CreateFromStartupInfo())
	defer proxy.Close()

	os.Exit(do(proxy))
}

type ErrDaemon string

func NewErrDaemon(str string) ErrDaemon {
	return ErrDaemon(fmt.Sprintf("amber_ctl: daemon error: %s", str))
}

func (e ErrDaemon) Error() string {
	return string(e)
}

func getUpdateComplete(proxy *amber.ControlInterface, name string, version *string, merkle *string) error {
	c, err := proxy.GetUpdateComplete(name, version, merkle)
	if err != nil {
		return NewErrDaemon(fmt.Sprintf("error making FIDL request: %s", err))
	}

	defer c.Close()
	b := make([]byte, 64*1024)
	for {
		sigs, err := zxwait.Wait(*c.Handle(),
			zx.SignalChannelPeerClosed|zx.SignalChannelReadable,
			zx.Sys_deadline_after(zx.Duration((3 * time.Second).Nanoseconds())))

		if err != nil {
			if zerr, ok := err.(zx.Error); ok && zerr.Status == zx.ErrTimedOut {
				log.Println("Awaiting response...")
				continue
			}
			return NewErrDaemon(
				fmt.Sprintf("unknown error while waiting for response from channel: %s", err))
		}

		if sigs&zx.SignalChannelReadable != 0 {
			bs, _, err := c.Read(b, []zx.Handle{}, 0)
			if err != nil {
				return NewErrDaemon(
					fmt.Sprintf("error reading response from channel: %s", err))
			}

			if sigs&zx.SignalUser0 != 0 {
				return NewErrDaemon(string(b[:bs]))
			}

			pkgname := name
			if version != nil {
				pkgname = filepath.Join(pkgname, *version)
			}
			if merkle != nil {
				pkgname = filepath.Join(pkgname, *merkle)
			}
			log.Printf("Success %s: %s", pkgname, string(b[:bs]))
			return nil
		}

		if sigs&zx.SignalChannelPeerClosed != 0 {
			return NewErrDaemon("response channel closed unexpectedly.")
		}
	}
}

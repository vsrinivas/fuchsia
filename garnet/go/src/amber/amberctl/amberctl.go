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
	"regexp"
	"strings"
	"syscall/zx"
	"time"
	"unicode"

	"amber/urlscope"
	"app/context"
	"fidl/fuchsia/amber"
	fuchsiaio "fidl/fuchsia/io"
	"fidl/fuchsia/pkg"
	"fidl/fuchsia/pkg/rewrite"
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

    add_repo_cfg  - add a repository config to the set of known repositories, using a source config
        -n: name of the update source (optional, with URL)
        -f: file path or url to a source config file
        -h: SHA256 hash of source config file (optional, with URL)

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

type Services struct {
	amber         *amber.ControlInterface
	resolver      *pkg.PackageResolverInterface
	repoMgr       *pkg.RepositoryManagerInterface
	rewriteEngine *rewrite.EngineInterface
}

func connectToAmber(ctx *context.Context) *amber.ControlInterface {
	req, pxy, err := amber.NewControlInterfaceRequest()
	if err != nil {
		panic(err)
	}
	ctx.ConnectToEnvService(req)
	return pxy
}

func connectToPackageResolver(ctx *context.Context) *pkg.PackageResolverInterface {
	req, pxy, err := pkg.NewPackageResolverInterfaceRequest()
	if err != nil {
		panic(err)
	}
	ctx.ConnectToEnvService(req)
	return pxy
}

func connectToRepositoryManager(ctx *context.Context) *pkg.RepositoryManagerInterface {
	req, pxy, err := pkg.NewRepositoryManagerInterfaceRequest()
	if err != nil {
		panic(err)
	}
	ctx.ConnectToEnvService(req)
	return pxy
}

func connectToRewriteEngine(ctx *context.Context) *rewrite.EngineInterface {
	req, pxy, err := rewrite.NewEngineInterfaceRequest()
	if err != nil {
		panic(err)
	}
	ctx.ConnectToEnvService(req)
	return pxy
}

// upgradeSourceConfig attempts to upgrade an amber.SourceConfig into a pkg.RepositoryConfig
//
// The two config formats are incompatible in various ways:
//
// * repo configs cannot be disabled. amberctl will attempt to preserve a config's disabled bit by
// not configuring a rewrite rule for the source.
//
// * repo configs do not support oauth, network client config options, or polling frequency
// overrides. If present, these options are discarded.
//
// * repo config mirrors do not accept different URLs for the TUF repo and the blobs. Any custom
// blob URL is discarded.
func upgradeSourceConfig(cfg amber.SourceConfig) pkg.RepositoryConfig {
	repoCfg := pkg.RepositoryConfig{
		RepoUrl:        repoUrlForId(cfg.Id),
		RepoUrlPresent: true,
	}

	mirror := pkg.MirrorConfig{
		MirrorUrl:        cfg.RepoUrl,
		MirrorUrlPresent: true,
		Subscribe:        cfg.Auto,
		SubscribePresent: true,
	}
	if cfg.BlobKey != nil {
		var blobKey pkg.RepositoryBlobKey
		blobKey.SetAesKey(cfg.BlobKey.Data[:])
		mirror.SetBlobKey(blobKey)
	}
	repoCfg.SetMirrors([]pkg.MirrorConfig{mirror})

	for _, key := range cfg.RootKeys {
		if key.Type != "ed25519" {
			continue
		}

		var rootKey pkg.RepositoryKeyConfig
		bytes, err := hex.DecodeString(key.Value)
		if err != nil {
			continue
		}
		rootKey.SetEd25519Key(bytes)

		repoCfg.RootKeys = append(repoCfg.RootKeys, rootKey)
		repoCfg.RootKeysPresent = true
	}

	return repoCfg
}

var invalidHostnameCharsPattern = regexp.MustCompile("[^a-zA-Z0-9_-]")

func sanitizeId(id string) string {
	return invalidHostnameCharsPattern.ReplaceAllString(id, "_")
}

func repoUrlForId(id string) string {
	return fmt.Sprintf("fuchsia-pkg://%s", sanitizeId(id))
}

func rewriteRuleForId(id string) rewrite.Rule {
	var rule rewrite.Rule
	rule.SetLiteral(rewrite.LiteralRule{
		HostMatch:             "fuchsia.com",
		HostReplacement:       sanitizeId(id),
		PathPrefixMatch:       "/",
		PathPrefixReplacement: "/",
	})
	return rule
}

func replaceDynamicRewriteRules(rewriteEngine *rewrite.EngineInterface, rule rewrite.Rule) error {
	return doRewriteRuleEditTransaction(rewriteEngine, func(transaction *rewrite.EditTransactionInterface) error {
		if err := transaction.ResetAll(); err != nil {
			return fmt.Errorf("fuchsia.pkg.rewrite.EditTransaction.ResetAll IPC encountered an error: %s", err)
		}

		s, err := transaction.Add(rule)
		if err != nil {
			return fmt.Errorf("fuchsia.pkg.rewrite.EditTransaction.Add IPC encountered an error: %s", err)
		}
		status := zx.Status(s)
		if status != zx.ErrOk {
			return fmt.Errorf("unable to add rewrite rule: %s", status)
		}

		return nil
	})
}

func removeAllDynamicRewriteRules(rewriteEngine *rewrite.EngineInterface) error {
	return doRewriteRuleEditTransaction(rewriteEngine, func(transaction *rewrite.EditTransactionInterface) error {
		if err := transaction.ResetAll(); err != nil {
			return fmt.Errorf("fuchsia.pkg.rewrite.EditTransaction.ResetAll IPC encountered an error: %s", err)
		}

		return nil
	})
}

// doRewriteRuleEditTransaction executes a rewrite rule edit transaction using
// the provided callback, retrying on data races a few times before giving up.
func doRewriteRuleEditTransaction(rewriteEngine *rewrite.EngineInterface, cb func(*rewrite.EditTransactionInterface) error) error {
	for i := 0; i < 10; i++ {
		err, status := func() (error, zx.Status) {
			var status zx.Status
			req, transaction, err := rewrite.NewEditTransactionInterfaceRequest()
			if err != nil {
				return fmt.Errorf("creating edit transaction: %s", err), status
			}
			defer transaction.Close()
			if err := rewriteEngine.StartEditTransaction(req); err != nil {
				return fmt.Errorf("fuchsia.pkg.rewrite.Engine IPC encountered an error: %s", err), status
			}

			if err := cb(transaction); err != nil {
				return err, status
			}

			s, err := transaction.Commit()
			if err != nil {
				return fmt.Errorf("fuchsia.pkg.rewrite.EditTransaction.Commit IPC encountered an error: %s", err), status
			}
			return nil, zx.Status(s)
		}()
		if err != nil {
			return err
		}
		switch status {
		case zx.ErrOk:
			return nil
		case zx.ErrUnavailable:
			continue
		default:
			return fmt.Errorf("unexpected error while committing rewrite rule transaction: %s", status)
		}
	}

	return fmt.Errorf("unable to commit rewrite rule changes")
}

func addSource(services Services, repoOnly bool) error {
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

	var cfg amber.SourceConfig
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

	if !repoOnly {
		added, err := services.amber.AddSrc(cfg)
		if err != nil {
			return fmt.Errorf("fuchsia.amber.Control IPC encountered an error: %s", err)
		}
		if !added {
			return fmt.Errorf("request arguments properly formatted, but possibly otherwise invalid")
		}

		if isSourceConfigEnabled(&cfg) && !*nonExclusive {
			if err := disableAllSources(services.amber, cfg.Id); err != nil {
				return err
			}
		}
	}

	repoCfg := upgradeSourceConfig(cfg)
	s, err := services.repoMgr.Add(repoCfg)
	if err != nil {
		return fmt.Errorf("fuchsia.pkg.RepositoryManager IPC encountered an error: %s", err)
	}
	status := zx.Status(s)
	if !(status == zx.ErrOk || status == zx.ErrAlreadyExists) {
		return fmt.Errorf("unable to register source with RepositoryManager: %s", status)
	}

	// Nothing currently registers sources in a disabled state, but make a best effort attempt
	// to try to prevent the source from being used anyway by only configuring a mapping of
	// fuchsia.com to this source if it is enabled. Note that this doesn't prevent resolving a
	// package using this config's id explicitly or calling an amber source config
	// "fuchsia.com".
	if !repoOnly && isSourceConfigEnabled(&cfg) {
		rule := rewriteRuleForId(cfg.Id)
		if err := replaceDynamicRewriteRules(services.rewriteEngine, rule); err != nil {
			return err
		}
	}

	return nil
}

func rmSource(services Services) error {
	name := strings.TrimSpace(*name)
	if name == "" {
		return fmt.Errorf("no source id provided")
	}

	status, err := services.amber.RemoveSrc(name)
	if err != nil {
		return fmt.Errorf("fuchsia.amber.Control IPC encountered an error: %s", err)
	}
	switch status {
	case amber.StatusOk:
		break
	case amber.StatusErrNotFound:
		return fmt.Errorf("Source not found")
	case amber.StatusErr:
		return fmt.Errorf("Unspecified error")
	default:
		return fmt.Errorf("Unexpected status: %v", status)
	}

	// Since modifications to amber.Control, RepositoryManager, and rewrite.Engine aren't
	// atomic and amberctl could be interrupted or encounter an error during any step,
	// unregister the rewrite rule before removing the repo config to prevent a dangling
	// rewrite rule to a repo that no longer exists.
	if err := removeAllDynamicRewriteRules(services.rewriteEngine); err != nil {
		return err
	}

	s, err := services.repoMgr.Remove(repoUrlForId(name))
	if err != nil {
		return fmt.Errorf("fuchsia.pkg.RepositoryManager IPC encountered an error: %s", err)
	}
	zxStatus := zx.Status(s)
	if !(zxStatus == zx.ErrOk || zxStatus == zx.ErrNotFound) {
		return fmt.Errorf("unable to remove source from RepositoryManager: %s", zxStatus)
	}

	return nil
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

func do(services Services) int {
	switch os.Args[1] {
	case "get_up":
		if err := getUp(services.resolver); err != nil {
			log.Printf("error getting an update: %s", err)
			return 1
		}
	case "get_blob":
		if *blobID == "" {
			log.Printf("no blob id provided")
			return 1
		}
		if err := services.amber.GetBlob(*blobID); err != nil {
			log.Printf("error requesting blob fetch: %s", err)
			return 1
		}
	case "add_repo_cfg":
		if err := addSource(services, true); err != nil {
			log.Printf("error adding repo: %s", err)
			if _, ok := err.(ErrGetFile); ok {
				return 2
			} else {
				return 1
			}
		}
	case "add_src":
		if err := addSource(services, false); err != nil {
			log.Printf("error adding source: %s", err)
			if _, ok := err.(ErrGetFile); ok {
				return 2
			} else {
				return 1
			}
		}
	case "rm_src":
		if err := rmSource(services); err != nil {
			log.Printf("error removing source: %s", err)
			return 1
		}
	case "list_srcs":
		if err := listSources(services.amber); err != nil {
			log.Printf("error listing sources: %s", err)
			return 1
		}
	case "check":
		log.Printf("%q not yet supported\n", os.Args[1])
		return 1
	case "test":
		if err := doTest(services.amber); err != nil {
			log.Printf("error testing connection to amber: %s", err)
			return 1
		}
	case "system_update":
		configured, err := services.amber.CheckForSystemUpdate()
		if err != nil {
			log.Printf("error checking for system update: %s", err)
			return 1
		}

		if configured {
			fmt.Printf("triggered a system update check\n")
		} else {
			fmt.Printf("system update is not configured\n")
		}
	case "enable_src":
		if *name == "" {
			log.Printf("Error enabling source: no source id provided")
			return 1
		}
		err := setSourceEnablement(services.amber, *name, true)
		if err != nil {
			log.Printf("Error enabling source: %s", err)
			return 1
		}
		err = replaceDynamicRewriteRules(services.rewriteEngine, rewriteRuleForId(*name))
		if err != nil {
			log.Printf("Error configuring rewrite rules: %s", err)
			return 1
		}
		fmt.Printf("Source %q enabled\n", *name)
		if !*nonExclusive {
			if err := disableAllSources(services.amber, *name); err != nil {
				log.Printf("Error disabling sources: %s", err)
				return 1
			}
		}
	case "disable_src":
		if *name == "" {
			log.Printf("Error disabling source: no source id provided")
			return 1
		}
		err := setSourceEnablement(services.amber, *name, false)
		if err != nil {
			log.Printf("Error disabling source: %s", err)
			return 1
		}
		err = removeAllDynamicRewriteRules(services.rewriteEngine)
		if err != nil {
			log.Printf("Error configuring rewrite rules: %s", err)
			return 1
		}
		fmt.Printf("Source %q disabled\n", *name)
	case "gc":
		err := services.amber.Gc()
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

	var services Services

	services.amber = connectToAmber(ctx)
	defer services.amber.Close()

	services.resolver = connectToPackageResolver(ctx)
	defer services.resolver.Close()

	services.repoMgr = connectToRepositoryManager(ctx)
	defer services.repoMgr.Close()

	services.rewriteEngine = connectToRewriteEngine(ctx)
	defer services.rewriteEngine.Close()

	os.Exit(do(services))
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

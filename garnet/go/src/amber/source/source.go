// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package source

import (
	"amber/atonce"
	"context"
	"crypto/aes"
	"crypto/cipher"
	"crypto/tls"
	"crypto/x509"
	"encoding/hex"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"io/ioutil"
	"log"
	"net"
	"net/http"
	"net/url"
	"os"
	"path/filepath"
	"regexp"
	"sync"
	"syscall"
	"time"

	"fidl/fuchsia/amber"

	"fuchsia.googlesource.com/sse"

	tuf "github.com/flynn/go-tuf/client"
	tuf_data "github.com/flynn/go-tuf/data"
	"golang.org/x/oauth2"
)

const (
	configFileName = "config.json"
)

var merklePat = regexp.MustCompile("^[0-9a-f]{64}$")

// ErrNoUpdate is returned if no update is available.
var ErrNoUpdate = errors.New("amber/source: no update available")

// ErrUnknownPkg is returned if the Source doesn't have any data about any
// version of the package.
var ErrUnknownPkg = errors.New("amber/source: package not known")

// ErrNoUpdateContent is returned if the requested package content couldn't be
// retrieved.
var ErrNoUpdateContent = errors.New("amber/source: update content not available")

type tufSourceConfig struct {
	Config *amber.SourceConfig

	Oauth2Token *oauth2.Token

	Status *SourceStatus
}

type SourceStatus struct {
	Enabled *bool
}

// LoadSourceConfigs loads source configs from a directory.  The directory
// structure looks like:
//
//     $dir/source1/config.json
//     $dir/source2/config.json
//     ...
//
// If an error is encountered loading any config, none are returned.
func LoadSourceConfigs(dir string) ([]*amber.SourceConfig, error) {
	files, err := ioutil.ReadDir(dir)
	if err != nil {
		return nil, err
	}

	configs := make([]*amber.SourceConfig, 0, len(files))
	for _, file := range files {
		p := filepath.Join(dir, file.Name(), configFileName)
		log.Printf("loading source config %s", p)

		cfg, err := LoadSourceConfigFromPath(p)
		if err != nil {
			return nil, err
		}
		configs = append(configs, cfg)
	}

	return configs, nil
}

func LoadSourceConfigFromPath(path string) (*amber.SourceConfig, error) {
	f, err := os.Open(path)
	if err != nil {
		return nil, err
	}
	defer f.Close()

	var cfg amber.SourceConfig
	if err := json.NewDecoder(f).Decode(&cfg); err != nil {
		return nil, err
	}

	// it is possible we encounter a config on disk that does not have
	// this value set, set the defaults
	if cfg.StatusConfig == nil {
		cfg.StatusConfig = &amber.StatusConfig{Enabled: true}
	}

	return &cfg, nil
}

func newSourceConfig(cfg *amber.SourceConfig) (tufSourceConfig, error) {
	if cfg.Id == "" {
		return tufSourceConfig{}, fmt.Errorf("tuf source id cannot be empty")
	}

	if _, err := url.ParseRequestURI(cfg.RepoUrl); err != nil {
		return tufSourceConfig{}, err
	}

	if len(cfg.RootKeys) == 0 {
		return tufSourceConfig{}, fmt.Errorf("no root keys provided")
	}

	status := false
	srcStatus := &SourceStatus{&status}
	if cfg.StatusConfig != nil && cfg.StatusConfig.Enabled {
		*srcStatus.Enabled = true
	}

	return tufSourceConfig{
		Config: cfg,
		Status: srcStatus,
	}, nil
}

// Source wraps a TUF Client into the Source interface
type Source struct {
	// mu guards all following fields
	mu sync.Mutex

	cfg tufSourceConfig

	dir string

	// We save a reference to the tuf local store so that when we update
	// the tuf client, we can reuse the store. This avoids us from having
	// multiple database connections to the same file, which could corrupt
	// the TUF database.
	localStore tuf.LocalStore

	tokenSource oauth2.TokenSource
	httpClient  *http.Client

	keys      []*tuf_data.Key
	tufClient *tuf.Client

	// TODO(raggi): can optimize startup by persisting this information, or by
	// loading the tuf metadata and inspecting the timestamp metadata.
	lastUpdate time.Time

	ctx    context.Context
	cancel context.CancelFunc
}

type custom struct {
	Merkle string `json:"merkle"`
	// Size sometimes not set, as it is in the process of being introduced. We use
	// the pointer to determine set state.
	Size *int64 `json:"size"`
}

type RemoteStoreError struct {
	error
}

type IOError struct {
	error
}

func NewSource(dir string, c *amber.SourceConfig) (*Source, error) {
	if dir == "" {
		return nil, fmt.Errorf("tuf source directory cannot be an empty string")
	}

	cfg, err := newSourceConfig(c)
	if err != nil {
		return nil, err
	}

	ctx, cancel := context.WithCancel(context.Background())

	src := Source{
		cfg:    cfg,
		dir:    dir,
		ctx:    ctx,
		cancel: cancel,
	}

	if err := src.initSource(); err != nil {
		return nil, err
	}

	return &src, nil
}

// setEnabledStatus examines the config to see if the Status field exists and
// if enabled is set. If either is not present the field is added and set to
// enabled. If the sourceConfig is changed true is returned, otherwise false.
func setEnabledStatus(cfg *tufSourceConfig) bool {
	dirty := false
	if cfg.Status == nil {
		enabled := true
		cfg.Status = &SourceStatus{&enabled}
		dirty = true
	} else if cfg.Status.Enabled == nil {
		enabled := true
		cfg.Status.Enabled = &enabled
		dirty = true
	}

	// it is possible we encounter a config on disk that does not have
	// this value set, set the defaults
	if cfg.Config.StatusConfig == nil {
		cfg.Config.StatusConfig = &amber.StatusConfig{Enabled: true}
		dirty = true
	}

	return dirty
}

func LoadSourceFromPath(dir string) (*Source, error) {
	log.Printf("loading source from %s", dir)

	f, err := os.Open(filepath.Join(dir, configFileName))
	if err != nil {
		return nil, err
	}
	defer f.Close()

	var cfg tufSourceConfig
	if err := json.NewDecoder(f).Decode(&cfg); err != nil {
		return nil, err
	}

	dirty := setEnabledStatus(&cfg)

	ctx, cancel := context.WithCancel(context.Background())

	src := Source{
		cfg:    cfg,
		dir:    dir,
		ctx:    ctx,
		cancel: cancel,
	}

	if dirty {
		src.Save()
	}

	if err := src.initSource(); err != nil {
		return nil, err
	}

	return &src, nil
}

func (f *Source) Enabled() bool {
	f.mu.Lock()
	defer f.mu.Unlock()

	return *f.cfg.Status.Enabled
}

// This function finishes initializing the Source by parsing out the config
// data to create the derived fields, like the TUFClient.
func (f *Source) initSource() error {
	f.mu.Lock()
	defer f.mu.Unlock()

	keys, err := newTUFKeys(f.cfg.Config.RootKeys)
	if err != nil {
		return err
	}
	f.keys = keys

	// We might have multiple things in the store directory, so put tuf in
	// it's own directory.
	localStore, err := NewFileStore(filepath.Join(f.dir, "tuf.json"))
	if err != nil {
		return IOError{fmt.Errorf("couldn't open datastore: %s", err)}
	}
	f.localStore = localStore

	return f.updateTUFClientLocked()
}

// Start starts background operations associated with this Source, such as
// token fetching and update source monitoring. This method should only be
// called once per active source.
func (f *Source) Start() {
	f.AutoWatch()
	f.updateRefreshToken()
}

func (f *Source) processTokenUpdate(clientId, token string) {
	if !f.Enabled() {
		return
	}

	f.mu.Lock()
	defer f.mu.Unlock()

	if clientId != f.cfg.Config.Oauth2Config.ClientId {
		log.Println("source: client ID is unexpected, dropping token")
		return
	}

	f.cfg.Oauth2Token = &oauth2.Token{RefreshToken: token, TokenType: "Bearer", Expiry: time.Now()}

	if err := f.updateTUFClientLocked(); err != nil {
		log.Printf("failed to update tuf client: %s", err)
		return
	}

	if err := f.saveLocked(); err != nil {
		log.Printf("failed to save config: %s", err)
		return
	}
}

func oauth2Config(c *amber.OAuth2Config) *oauth2.Config {
	if c == nil {
		return nil
	}
	return &oauth2.Config{
		ClientID:     c.ClientId,
		ClientSecret: c.ClientSecret,
		Endpoint: oauth2.Endpoint{
			AuthURL:  c.AuthUrl,
			TokenURL: c.TokenUrl,
		},
		Scopes: c.Scopes,
	}
}

func oauth2HttpClient(httpClient *http.Client, cfg *oauth2.Config,
	token *oauth2.Token) (*http.Client, oauth2.TokenSource) {
	if cfg == nil {
		return httpClient, nil
	}

	// If we have oauth2 configured, we need to wrap the client in order to
	// inject the authentication header.
	var tokenSource oauth2.TokenSource
	// Store the client in the context so oauth2 can use it to
	// fetch the token. This client's transport will also be used
	// as the base of the client oauth2 returns to us, except for
	// the request timeout, which we manually have to copy over.
	ctx := context.WithValue(context.Background(), oauth2.HTTPClient, httpClient)

	timeout := httpClient.Timeout

	tokenSource = cfg.TokenSource(ctx, token)
	httpClient = oauth2.NewClient(ctx, tokenSource)

	httpClient.Timeout = timeout

	return httpClient, tokenSource
}

// Initialize (or reinitialize) the TUFClient. This is especially useful when
// logging in, or modifying any of the http settings since there's no way to
// change settings in place, so we need to replace it with a new tuf.Client.
//
// NOTE: It's the responsibility of the caller to hold the mutex before calling
// this function.
func (f *Source) updateTUFClientLocked() error {
	httpClient, err := newHTTPClient(f.cfg.Config.TransportConfig)
	if err != nil {
		return err
	}

	authConfig := oauth2Config(f.cfg.Config.Oauth2Config)
	httpClient, tokenSource := oauth2HttpClient(httpClient, authConfig, f.cfg.Oauth2Token)

	// Create a new tuf client that uses the new http client.
	remoteStore, err := tuf.HTTPRemoteStore(f.cfg.Config.RepoUrl, nil, httpClient)
	if err != nil {
		return RemoteStoreError{fmt.Errorf("server address not understood: %s", err)}
	}
	tufClient := tuf.NewClient(f.localStore, remoteStore)

	// We got our tuf client ready to go. Before we store the client in our
	// source, make sure to close the old client's transport's idle
	// connections so we don't leave a bunch of sockets open.
	f.closeIdleConnections()

	// We're done! Save the clients for the next time we update our source.
	f.tokenSource = tokenSource
	f.httpClient = httpClient
	f.tufClient = tufClient

	return nil
}

func newHTTPClient(cfg *amber.TransportConfig) (*http.Client, error) {
	// Create our transport with default settings copied from Go's
	// `http.DefaultTransport`. We can't just copy the default because it
	// contains some mutexes, and copying it may leave the transport in an
	// inconsistent state.
	t := &http.Transport{
		Proxy: http.ProxyFromEnvironment,
		DialContext: (&net.Dialer{
			Timeout:   30 * time.Second,
			KeepAlive: 30 * time.Second,
			DualStack: true,
		}).DialContext,
		MaxIdleConns: 100,
		// The following setting is non-default:
		MaxConnsPerHost:       50,
		IdleConnTimeout:       90 * time.Second,
		TLSHandshakeTimeout:   10 * time.Second,
		ExpectContinueTimeout: 1 * time.Second,
		TLSClientConfig:       nil,
		// A workaround for TC-243 where closed connections are not being
		// properly detected. This is a mitigation for PKG-400
		ResponseHeaderTimeout: 15 * time.Second,
	}

	if cfg == nil {
		return &http.Client{Transport: t}, nil
	}

	tlsClientConfig, err := newTLSClientConfig(cfg.TlsClientConfig)
	if err != nil {
		return nil, err
	}
	t.TLSClientConfig = tlsClientConfig

	if cfg.ConnectTimeout != 0 || cfg.KeepAlive != 0 {
		t.DialContext = (&net.Dialer{
			Timeout:   time.Duration(cfg.ConnectTimeout) * time.Millisecond,
			KeepAlive: time.Duration(cfg.KeepAlive) * time.Millisecond,
			DualStack: true,
		}).DialContext
	}

	if cfg.MaxIdleConns != 0 {
		t.MaxIdleConns = int(cfg.MaxIdleConns)
	}

	if cfg.MaxIdleConnsPerHost != 0 {
		t.MaxIdleConnsPerHost = int(cfg.MaxIdleConnsPerHost)
	}

	if cfg.IdleConnTimeout != 0 {
		t.IdleConnTimeout = time.Duration(cfg.IdleConnTimeout) * time.Millisecond
	}

	if cfg.ResponseHeaderTimeout != 0 {
		t.ResponseHeaderTimeout = time.Duration(cfg.ResponseHeaderTimeout) * time.Millisecond
	}

	if cfg.TlsHandshakeTimeout != 0 {
		t.TLSHandshakeTimeout = time.Duration(cfg.TlsHandshakeTimeout) * time.Millisecond
	}

	if cfg.ExpectContinueTimeout != 0 {
		t.ExpectContinueTimeout = time.Duration(cfg.ExpectContinueTimeout) * time.Millisecond
	}

	c := &http.Client{
		Transport: t,
	}

	if cfg.RequestTimeout != 0 {
		c.Timeout = time.Duration(cfg.RequestTimeout) * time.Millisecond
	}

	return c, nil
}

func newTLSClientConfig(cfg *amber.TlsClientConfig) (*tls.Config, error) {
	if cfg == nil {
		return nil, nil
	}

	t := &tls.Config{
		InsecureSkipVerify: cfg.InsecureSkipVerify,
	}

	if len(cfg.RootCAs) != 0 {
		t.RootCAs = x509.NewCertPool()
		for _, ca := range cfg.RootCAs {
			if !t.RootCAs.AppendCertsFromPEM([]byte(ca)) {
				log.Printf("failed to add cert")
				return nil, fmt.Errorf("failed to add certificate")
			}
		}
	}

	return t, nil
}

// Note, the mutex should be held when this is called.
func (f *Source) initLocalStoreLocked() error {
	if needsInit(f.localStore) {
		log.Print("initializing local TUF store")
		err := f.tufClient.Init(f.keys, len(f.keys))
		if err != nil {
			return fmt.Errorf("TUF init failed: %s", err)
		}
	}

	return nil
}

func newTUFKeys(cfg []amber.KeyConfig) ([]*tuf_data.Key, error) {
	keys := make([]*tuf_data.Key, len(cfg))

	for i, key := range cfg {
		if key.Type != "ed25519" {
			return nil, fmt.Errorf("unsupported key type %s", key.Type)
		}

		keyHex, err := hex.DecodeString(key.Value)
		if err != nil {
			return nil, fmt.Errorf("invalid key value: %s", err)
		}

		keys[i] = &tuf_data.Key{
			Type:  key.Type,
			Value: tuf_data.KeyValue{Public: tuf_data.HexBytes(keyHex)},
		}
	}

	return keys, nil
}

func (f *Source) GetId() string {
	return f.cfg.Config.Id
}

func (f *Source) GetConfig() *amber.SourceConfig {
	return f.cfg.Config
}

func (f *Source) SetEnabled(enabled bool) {
	f.mu.Lock()
	f.cfg.Status.Enabled = &enabled
	f.mu.Unlock()
}

// try to start a refresh token update. This should not be called while holding
// the struct mutex as it will be locked during this call
func (f *Source) updateRefreshToken() {
	if !f.Enabled() {
		return
	}

	f.mu.Lock()
	defer f.mu.Unlock()

	if f.cfg.Config.Oauth2Config == nil {
		return
	}
	go defaultTokenLoader.ReadToken(f.processTokenUpdate, f.cfg.Config.Oauth2Config.ClientId)
}

func (f *Source) GetHttpClient() (*http.Client, error) {
	f.mu.Lock()
	defer f.mu.Unlock()

	if err := f.refreshOauth2TokenLocked(); err != nil {
		return nil, fmt.Errorf("failed to refresh oauth2 token: %s", err)
	}

	// http.Client itself is thread safe, but the member alias is not, so is
	// guarded here.
	return f.httpClient, nil
}

// Check if the token has refreshed. If so, save a new token
func (f *Source) refreshOauth2TokenLocked() error {
	if f.cfg.Oauth2Token == nil {
		return nil
	}

	// Grab the latest token from the token source. If the token has
	// expired, it will automatically refresh it in the background and give
	// us a new access token.
	newToken, err := f.tokenSource.Token()
	if err != nil {
		return err
	}

	if newToken.AccessToken != f.cfg.Oauth2Token.AccessToken {
		log.Printf("refreshed oauth2 token for: %s", f.cfg.Config.Id)
		f.cfg.Oauth2Token = newToken
		f.saveLocked()
	}

	return nil
}

// UpdateIfStale updates this source if the source has not recently updated.
func (f *Source) UpdateIfStale() error {
	f.mu.Lock()
	maxAge := time.Duration(f.cfg.Config.RatePeriod) * time.Second
	needsUpdate := time.Since(f.lastUpdate) > maxAge
	f.mu.Unlock()
	if needsUpdate {
		return f.Update()
	}
	return nil
}

// Update requests updated metadata from this source, returning any error that
// ocurred during the process.
func (f *Source) Update() error {
	return atonce.Do("source", f.cfg.Config.Id, func() error {
		f.mu.Lock()
		defer f.mu.Unlock()

		if !*f.cfg.Status.Enabled {
			return nil
		}

		// update any relevant auth token before initializing the tuf client
		// because the tuf client will try to use the HTTP context when
		// initializing
		if err := f.refreshOauth2TokenLocked(); err != nil {
			return fmt.Errorf("tuf_source: failed to refresh oauth2 token: %s", err)
		}

		if err := f.initLocalStoreLocked(); err != nil {
			return fmt.Errorf("tuf_source: source could not be initialized: %s", err)
		}

		_, err := f.tufClient.Update()
		if tuf.IsLatestSnapshot(err) || err == nil {
			f.lastUpdate = time.Now()
			err = nil
		}
		return err
	})
}

// MerkleFor looks up a package target from the available TUF targets, returning the merkleroot and plaintext object length, or an error.
func (f *Source) MerkleFor(name, version string) (string, int64, error) {
	f.mu.Lock()
	defer f.mu.Unlock()

	m, err := f.tufClient.Targets()

	if err != nil {
		return "", 0, fmt.Errorf("tuf_source: error reading TUF tarets: %s", err)
	}

	if version == "" {
		version = "0"
	}

	target := fmt.Sprintf("/%s/%s", name, version)
	meta, ok := m[target]
	if !ok {
		return "", 0, ErrUnknownPkg
	}

	if meta.Custom == nil {
		return "", 0, ErrUnknownPkg
	}

	custom := custom{}
	if err := json.Unmarshal(*meta.Custom, &custom); err != nil {
		return "", 0, fmt.Errorf("error parsing merkle metadata: %s", err)
	}

	if !merklePat.MatchString(custom.Merkle) {
		log.Printf("tuf_source: found target %q, but has invalid merkle metadata: %q", target, custom.Merkle)
		return "", 0, ErrUnknownPkg
	}

	// If a blob is encrypted then the TUF recorded length includes the iv block,
	// but we want to return the plain text content size.
	length := meta.Length
	if custom.Size != nil {
		length = *custom.Size
	}

	return custom.Merkle, length, nil
}

func (f *Source) Save() error {
	f.mu.Lock()
	defer f.mu.Unlock()

	return f.saveLocked()
}

func (f *Source) DeleteConfig() error {
	f.mu.Lock()
	defer f.mu.Unlock()

	return os.Remove(filepath.Join(f.dir, configFileName))
}

func (f *Source) Delete() error {
	f.mu.Lock()
	defer f.mu.Unlock()

	return os.RemoveAll(f.dir)
}

func (f *Source) Close() {
	f.cancel()
	f.closeIdleConnections()
}

func (f *Source) closeIdleConnections() {
	if f.httpClient != nil {
		transport := f.httpClient.Transport
		if transport == nil {
			transport = http.DefaultTransport
		}
		if transport, ok := transport.(*http.Transport); ok {
			transport.CloseIdleConnections()
		}
	}
}

// Actually save the config.
//
// NOTE: It's the responsibility of the caller to hold the mutex before calling
// this function.
func (f *Source) saveLocked() error {
	err := os.MkdirAll(f.dir, os.ModePerm)
	if err != nil {
		return err
	}

	p := filepath.Join(f.dir, configFileName)

	// We want to atomically write the config, so we'll first write it to a
	// temp file, then do an atomic rename to overwrite the target.

	file, err := ioutil.TempFile(f.dir, configFileName)
	if err != nil {
		return err
	}
	defer file.Close()

	// Make sure to clean up the temp file if there's an error.
	defer func() {
		if err != nil {
			os.Remove(file.Name())
		}
	}()

	// Encode the cfg as a pretty printed json.
	encoder := json.NewEncoder(file)
	encoder.SetIndent("", "    ")

	if err = encoder.Encode(f.cfg); err != nil {
		return err
	}

	if err = file.Close(); err != nil {
		return err
	}

	if err := os.Rename(file.Name(), p); err != nil {
		return err
	}

	// We do not report an error back to the caller down the sync path, as our
	// state change did complete in the running system. The state of the filesystem
	// is undefined, and we can not provide meaningful information ourselves.
	d, err := os.OpenFile(f.dir, syscall.O_RDONLY|syscall.O_DIRECTORY, 0600)
	if err != nil {
		log.Printf("save config error: open dir for sync: %s", err)
		return nil
	}
	defer d.Close()
	if err := d.Sync(); err != nil {
		log.Printf("save config error: sync: %s", err)
		return nil
	}
	return nil
}

func needsInit(s tuf.LocalStore) bool {
	meta, err := s.GetMeta()
	if err != nil {
		return true
	}

	_, found := meta["root.json"]
	return !found
}

func (f *Source) requestBlob(blob string) (*http.Response, error) {
	client, err := f.GetHttpClient()
	if err != nil {
		return nil, err
	}

	blobUrl := f.GetConfig().BlobRepoUrl
	if blobUrl == "" {
		blobUrl = f.GetConfig().RepoUrl + "/blobs"
	}
	u, err := url.Parse(blobUrl)
	if err != nil {
		return nil, err
	}
	u.Path = filepath.Join(u.Path, blob)

	return client.Get(u.String())
}

func (f *Source) FetchInto(blob string, length int64, outputDir string) error {
	dst, err := os.OpenFile(filepath.Join(outputDir, blob), os.O_CREATE|os.O_WRONLY, os.ModePerm)
	if err != nil {
		return err
	}
	defer dst.Close()

	resp, err := f.requestBlob(blob)
	if err != nil {
		return err
	}
	defer resp.Body.Close()

	if resp.StatusCode != 200 {
		return fmt.Errorf("fetch %q failed with code %d", blob, resp.StatusCode)
	}

	if resp.ContentLength == -1 && length == -1 {
		return fmt.Errorf("unknown content length, can not write")
	}

	var src io.Reader = resp.Body
	gotLength := resp.ContentLength
	if f.cfg.Config.BlobKey != nil {
		gotLength -= aes.BlockSize
		block, err := aes.NewCipher(f.cfg.Config.BlobKey.Data[:])
		if err != nil {
			return err
		}
		iv := make([]byte, aes.BlockSize)
		if _, err := io.ReadFull(src, iv); err != nil {
			return err
		}
		stream := cipher.NewCTR(block, iv)
		src = cipher.StreamReader{
			stream,
			src,
		}
	}

	if gotLength > -1 && length > -1 && gotLength != length {
		return fmt.Errorf("bad content length: %d, expected %d", gotLength, length)
	}

	if length > -1 {
		src = io.LimitReader(src, length)
		err = dst.Truncate(length)
	} else {
		src = io.LimitReader(src, gotLength)
		err = dst.Truncate(gotLength)
	}

	if err != nil {
		return err
	}

	written, err := io.Copy(dst, src)
	if err != nil {
		return err
	}

	if gotLength > -1 && written != gotLength {
		return fmt.Errorf("blob incomplete, only wrote %d out of %d bytes", written, gotLength)
	}

	if length > -1 && written != length {
		return fmt.Errorf("blob incomplete, only wrote %d out of %d bytes", written, length)
	}

	return nil
}

func (f *Source) AutoWatch() {
	if !f.Enabled() || !f.cfg.Config.Auto {
		return
	}
	go func() {
		for {
			if !f.Enabled() {
				return
			}

			req, err := http.NewRequest("GET", f.cfg.Config.RepoUrl+"/auto", nil)
			if err != nil {
				log.Printf("autowatch terminal error: %q: %s", f.cfg.Config.RepoUrl, err)
				return
			}
			req.Header.Add("Accept", "text/event-stream")
			cli, err := f.GetHttpClient()
			if err != nil {
				log.Printf("autowatch error for %q: %s", f.cfg.Config.RepoUrl, err)
				time.Sleep(time.Minute)
				continue
			}
			r, err := cli.Do(req.WithContext(f.ctx))
			if err != nil {
				if e, ok := err.(*url.Error); ok {
					if e.Err == context.Canceled {
						break
					}
				}
				log.Printf("autowatch error for %q: %s", f.cfg.Config.RepoUrl, err)
				time.Sleep(time.Minute)
				continue
			}
			c, err := sse.New(r)
			if err != nil {
				log.Printf("autowatch error for %q: %s", f.cfg.Config.RepoUrl, err)
				time.Sleep(time.Minute)
				continue
			}
			for {
				_, err := c.ReadEvent()
				if err != nil {
					break
				}
				if !f.Enabled() {
					return
				}
				f.Update()
			}
		}
	}()
}

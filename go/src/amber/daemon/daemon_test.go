// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package daemon

import (
	"fmt"
	"io/ioutil"
	"math/rand"
	"net/http"
	"os"
	"path/filepath"
	"sort"
	"sync"
	"testing"
	"time"

	"amber/pkg"
	"amber/source"

	"fidl/fuchsia/amber"
)

var letters = []rune("1234567890abcdef")

const testSrcsPath = "/system/data/amber/test_sources"

func randSeq(n int) string {
	rand.Seed(time.Now().UnixNano())
	runeLen := len(letters)
	b := make([]rune, n)
	for i := range b {
		b[i] = letters[rand.Intn(runeLen)]
	}
	return string(b)
}

type testSrc struct {
	mu         sync.Mutex
	id         string
	UpdateReqs map[string]int
	getReqs    map[pkg.Package]*struct{}
	interval   time.Duration
	pkgs       map[string]struct{}
	replyDelay time.Duration
	limit      uint64
	cfg        *amber.SourceConfig
}

func (t *testSrc) GetId() string {
	return t.id
}

func (t *testSrc) GetConfig() *amber.SourceConfig {
	if t.cfg == nil {
		t.cfg = &amber.SourceConfig{StatusConfig: &amber.StatusConfig{true}}
		t.cfg.Id = t.id
		t.cfg.BlobRepoUrl = "https://127.0.0.1:8083/test"
		t.cfg.RepoUrl = "https://127.0.0.1:8083/test/blobs"
	}

	return t.cfg
}

func (t *testSrc) GetHttpClient() (*http.Client, error) {
	return nil, nil
}

func (t *testSrc) Login() (*amber.DeviceCode, error) {
	return nil, fmt.Errorf("Login() is not implemented")
}

func (t *testSrc) AvailableUpdates(pkgs []*pkg.Package) (map[pkg.Package]pkg.Package, error) {
	t.mu.Lock()
	time.Sleep(t.replyDelay)
	updates := make(map[pkg.Package]pkg.Package)
	for _, p := range pkgs {
		if _, ok := t.pkgs[p.Name]; !ok {
			continue
		}
		t.UpdateReqs[p.Name] = t.UpdateReqs[p.Name] + 1
		up := pkg.Package{Name: p.Name, Version: randSeq(6)}
		t.getReqs[up] = &struct{}{}
		updates[*p] = up
	}

	t.mu.Unlock()
	return updates, nil
}

func (t *testSrc) FetchPkg(pkg *pkg.Package) (*os.File, error) {
	t.mu.Lock()
	defer t.mu.Unlock()
	if _, ok := t.getReqs[*pkg]; !ok {
		fmt.Println("ERROR: unknown update pkg requested")
		return nil, source.ErrNoUpdateContent
	}

	delete(t.getReqs, *pkg)
	return nil, nil
}

func (t *testSrc) CheckInterval() time.Duration {
	return t.interval
}

func (t *testSrc) Equals(o source.Source) bool {
	switch p := o.(type) {
	case *testSrc:
		return t == p
	default:
		return false
	}
}

func (t *testSrc) CheckLimit() uint64 {
	return t.limit
}

func (t *testSrc) Save() error {
	return nil
}

func (t *testSrc) DeleteConfig() error {
	return nil
}

func (t *testSrc) Delete() error {
	return nil
}

func (t *testSrc) Close() {
	return
}

func (t *testSrc) Enabled() bool {
	return t.cfg.StatusConfig.Enabled
}

func (t *testSrc) SetEnabled(enabled bool) {
	t.cfg.StatusConfig.Enabled = enabled
}

type testTicker struct {
	i    time.Duration
	last time.Time
	C    chan time.Time
}

func testBuildTicker(d time.Duration, tickerGroup *sync.WaitGroup, mu *sync.Mutex) (*time.Ticker, testTicker) {
	mu.Lock()
	defer mu.Unlock()
	defer tickerGroup.Done()

	c := make(chan time.Time)
	t := time.NewTicker(d)
	t.C = c

	tt := testTicker{i: d, last: time.Now(), C: c}
	return t, tt
}

func (t *testTicker) makeTick() {
	t.last = t.last.Add(t.i)
	t.C <- t.last
}

func processPackage(r *GetResult, pkgs *pkg.PackageSet) error {
	if r.Err != nil {
		return r.Err
	}
	pkgs.Replace(&r.Orig, &r.Update, false)
	return nil
}

func TestSourceEnablementForAddedSources(t *testing.T) {
	newTicker = time.NewTicker
	srcConfigs, err := source.LoadSourceConfigs(testSrcsPath)
	if err != nil {
		t.Fatalf("failed loading test source configs from package: %s", err)
	}

	store, err := ioutil.TempDir("", "")
	if err != nil {
		t.Error(err)
	}
	defer os.RemoveAll(store)

	d, err := NewDaemon(store, pkg.NewPackageSet(), processPackage, []source.Source{})
	if err != nil {
		t.Error(err)
	}

	for _, cfg := range srcConfigs {
		d.AddTUFSource(cfg)
	}

	srcIDs := make(map[string]struct{})
	for _, s := range srcConfigs {
		srcIDs[s.Id] = struct{}{}
	}

	srcs := d.GetActiveSources()
	for k := range srcs {
		delete(srcIDs, k)
	}
	if len(srcIDs) > 0 {
		t.Fatal("Not all sources were initially added")
	}

	// try disabling a source and make sure that it doesn't still show up
	err = d.DisableSource(srcConfigs[0].Id)
	if err != nil {
		t.Fatalf("Failure disabling source: %s", err)
	}

	updatedConfigs := srcConfigs[1:]
	srcIDs = make(map[string]struct{})

	for _, s := range updatedConfigs {
		srcIDs[s.Id] = struct{}{}
	}

	srcs = d.GetActiveSources()
	if len(srcs) != len(updatedConfigs) {
		t.Fatalf("expected %d enabled sources, but found %d", len(updatedConfigs), len(srcs))
	}

	for sID := range srcs {
		delete(srcIDs, sID)
	}
	if len(srcIDs) > 0 {
		t.Fatalf("%d sources which were expected to be enabled were not found", len(srcIDs))
	}

	err = d.EnableSource(srcConfigs[0].Id)
	if err != nil {
		t.Fatalf("Failed enabling source: %s", err)
	}

	srcs = d.GetActiveSources()
	if len(srcs) != len(srcConfigs) {
		t.Fatalf("expected %d enabled sources, but found %d", len(srcConfigs), len(srcs))
	}

	srcIDs = make(map[string]struct{})
	for _, s := range srcConfigs {
		srcIDs[s.Id] = struct{}{}
	}

	for sID := range srcs {
		delete(srcIDs, sID)
	}
	if len(srcIDs) != 0 {
		t.Fatalf("expected all sources enabled, but %d were not", len(srcIDs))
	}
}

func TestSourceEnablementForLoadedSources(t *testing.T) {
	// first create a daemon which adds a source and we disable that source.
	// Then create a new daemon that uses the same data store and therefore
	// know about that source and try to enable it.
	newTicker = time.NewTicker
	srcConfigs, err := source.LoadSourceConfigs(testSrcsPath)
	if err != nil {
		t.Fatalf("failed loading test source configs from package: %s", err)
	}

	store, err := ioutil.TempDir("", "")
	if err != nil {
		t.Error(err)
	}
	defer os.RemoveAll(store)

	d, err := NewDaemon(store, pkg.NewPackageSet(), processPackage, []source.Source{})
	if err != nil {
		t.Error(err)
	}

	d.AddTUFSource(srcConfigs[0])
	if err := d.DisableSource(srcConfigs[0].Id); err != nil {
		t.Fatalf("error disabling source: %s", err)
	}
	srcs := d.GetActiveSources()
	if len(srcs) != 0 {
		t.Fatalf("failed disabling source")
	}
	d.CancelAll()

	d, err = NewDaemon(store, pkg.NewPackageSet(), processPackage, []source.Source{})
	srcs = d.GetSources()
	if len(srcs) != 1 {
		t.Fatalf("disabled source not available in new Daemon")
	}

	srcs = d.GetActiveSources()
	if len(srcs) != 0 {
		t.Fatalf("disabled source is present in active sources")
	}

	if err = d.EnableSource(srcConfigs[0].Id); err != nil {
		t.Fatalf("error enabling source: %s", err)
	}

	srcs = d.GetActiveSources()
	if len(srcs) != 1 {
		t.Fatalf("source failed to enable")
	}
	d.CancelAll()
}

// TestDaemon tests daemon.go with a fake package source.
func TestDaemon(t *testing.T) {
	tickers := []testTicker{}
	muTickers := sync.Mutex{}
	tickerGroup := sync.WaitGroup{}

	newTicker = func(d time.Duration) *time.Ticker {
		t, tt := testBuildTicker(d, &tickerGroup, &muTickers)
		tickers = append(tickers, tt)
		tickerGroup.Done()
		return t
	}
	defer func() { newTicker = time.NewTicker }()

	// wait for one signal from building the ticker itself and one
	// from appending it to the tickers list
	tickerGroup.Add(2)

	testSrcs := createTestSrcs()
	pkgSet := createMonitorPkgs()
	sources := make([]source.Source, 0, len(testSrcs))
	for _, src := range testSrcs {
		// allow very high request rates for this test since rate limiting isn't
		// really the target of this test
		src.limit = 3
		src.interval = 1 * time.Nanosecond
		sources = append(sources, src)
	}

	store, err := ioutil.TempDir("", "")
	if err != nil {
		t.Error(err)
	}
	defer os.RemoveAll(store)

	d, err := NewDaemon(store, pkgSet, processPackage, sources)
	if err != nil {
		t.Error(err)
	}

	tickerGroup.Wait()
	// protect against improper test rewrites
	if len(tickers) != 1 {
		t.Errorf("Unexpected number of tickers! %d", len(tickers))
	}

	// run 10 times with a slight separation so as not to exceed the
	// throttle rate
	runs := 10
	for i := 0; i < runs; i++ {
		time.Sleep(10 * time.Nanosecond)
		tickers[0].makeTick()
	}
	// one final sleep to allow the last request to sneak through
	time.Sleep(10 * time.Millisecond)

	d.CancelAll()

	verifyReqCount(t, testSrcs, pkgSet, runs+1)
}

func TestGetRequest(t *testing.T) {
	emailPkg := pkg.Package{Name: "email", Version: "23af90ee"}
	videoPkg := pkg.Package{Name: "video", Version: "f2b8006c"}
	srchPkg := pkg.Package{Name: "search", Version: "fa08207e"}

	// create some test sources where neither has the full pkg set and
	// they overlap
	pkgs := make(map[string]struct{})
	pkgs[emailPkg.Name] = struct{}{}
	pkgs[videoPkg.Name] = struct{}{}
	srcRateLimit := time.Millisecond * 1
	tSrc := testSrc{
		id:         "src",
		UpdateReqs: make(map[string]int),
		getReqs:    make(map[pkg.Package]*struct{}),
		interval:   srcRateLimit,
		pkgs:       pkgs,
		limit:      1}

	pkgs = make(map[string]struct{})
	pkgs[videoPkg.Name] = struct{}{}
	pkgs[srchPkg.Name] = struct{}{}
	tSrc2 := testSrc{
		id:         "src2",
		UpdateReqs: make(map[string]int),
		getReqs:    make(map[pkg.Package]*struct{}),
		interval:   srcRateLimit,
		pkgs:       pkgs,
		limit:      1,
	}
	sources := []source.Source{&tSrc, &tSrc2}

	tickers := []testTicker{}
	muTickers := sync.Mutex{}
	tickerGroup := sync.WaitGroup{}

	newTicker = func(d time.Duration) *time.Ticker {
		t, tt := testBuildTicker(d, &tickerGroup, &muTickers)
		tickers = append(tickers, tt)
		return t
	}
	defer func() { newTicker = time.NewTicker }()

	tickerGroup.Add(1)

	store, err := ioutil.TempDir("", "")
	if err != nil {
		t.Error(err)
	}
	defer os.RemoveAll(store)

	d, err := NewDaemon(store, pkg.NewPackageSet(), processPackage, sources)
	if err != nil {
		t.Error(err)
	}

	tickerGroup.Wait()

	pkgSet := pkg.NewPackageSet()
	pkgSet.Add(&emailPkg)
	pkgSet.Add(&videoPkg)
	pkgSet.Add(&srchPkg)
	updateRes := d.GetUpdates(pkgSet)
	verifyGetResults(t, pkgSet, updateRes)

	time.Sleep(srcRateLimit * 2)
	pkgSet = pkg.NewPackageSet()
	pkgSet.Add(&videoPkg)
	updateRes = d.GetUpdates(pkgSet)
	verifyGetResults(t, pkgSet, updateRes)

	d.CancelAll()
}

func TestRateLimit(t *testing.T) {
	srcRateLimit := 20 * time.Millisecond
	tSrc := testSrc{
		id:         "src",
		UpdateReqs: make(map[string]int),
		getReqs:    make(map[pkg.Package]*struct{}),
		interval:   srcRateLimit,
		pkgs:       make(map[string]struct{}),
		limit:      1,
	}
	wrapped := NewSourceKeeper(&tSrc)
	dummy := []*pkg.Package{&pkg.Package{Name: "None", Version: "aaaaaa"}}

	if _, err := wrapped.AvailableUpdates(dummy); err == ErrRateExceeded {
		t.Errorf("Initial request was rate limited unexpectedly.\n")
	}

	if _, err := wrapped.AvailableUpdates(dummy); err != ErrRateExceeded {
		t.Errorf("Request was not rate limited\n")
	}

	time.Sleep(srcRateLimit)
	if _, err := wrapped.AvailableUpdates(dummy); err == ErrRateExceeded {
		t.Errorf("Rate-allowed request failed.\n")
	}
}

func TestRequestCollapse(t *testing.T) {
	pkgSet := pkg.NewPackageSet()
	emailPkg := pkg.Package{Name: "email", Version: "23af90ee"}
	videoPkg := pkg.Package{Name: "video", Version: "f2b8006c"}
	srchPkg := pkg.Package{Name: "search", Version: "fa08207e"}
	pkgSet.Add(&emailPkg)
	pkgSet.Add(&videoPkg)
	pkgSet.Add(&srchPkg)

	// create some test sources where neither has the full pkg set and
	// they overlap
	pkgs := make(map[string]struct{})
	pkgs[emailPkg.Name] = struct{}{}
	pkgs[videoPkg.Name] = struct{}{}
	srcRateLimit := time.Millisecond
	replyDelay := 20 * time.Millisecond
	tSrc := testSrc{
		id:         "src",
		UpdateReqs: make(map[string]int),
		getReqs:    make(map[pkg.Package]*struct{}),
		interval:   srcRateLimit,
		pkgs:       pkgs,
		limit:      1,
	}

	pkgs = make(map[string]struct{})
	pkgs[videoPkg.Name] = struct{}{}
	pkgs[srchPkg.Name] = struct{}{}
	tSrc2 := testSrc{
		id:         "src2",
		UpdateReqs: make(map[string]int),
		getReqs:    make(map[pkg.Package]*struct{}),
		interval:   srcRateLimit,
		pkgs:       pkgs,
		limit:      1,
	}
	sources := []source.Source{&tSrc, &tSrc2}
	testSrcs := []*testSrc{&tSrc, &tSrc2}

	tickers := []testTicker{}
	muTickers := sync.Mutex{}
	tickerGroup := sync.WaitGroup{}

	newTicker = func(d time.Duration) *time.Ticker {
		t, tt := testBuildTicker(d, &tickerGroup, &muTickers)
		tickers = append(tickers, tt)
		return t
	}
	defer func() { newTicker = time.NewTicker }()

	tickerGroup.Add(1)

	for _, src := range testSrcs {
		// introduce a reply delay so we can make sure to run
		// simultaneously
		src.replyDelay = replyDelay
	}

	store, err := ioutil.TempDir("", "")
	if err != nil {
		t.Error(err)
	}
	defer os.RemoveAll(store)

	d, err := NewDaemon(store, pkg.NewPackageSet(), processPackage, sources)
	if err != nil {
		t.Error(err)
	}

	tickerGroup.Wait()

	// we expect to generate only one request, since whichever arrives
	// second should just subscribe to the results of the first
	go d.GetUpdates(pkgSet)
	time.Sleep(2 * srcRateLimit)
	updateRes := d.GetUpdates(pkgSet)
	verifyReqCount(t, testSrcs, pkgSet, 1)
	verifyGetResults(t, pkgSet, updateRes)

	// verify that if we do two more requests sequentially that the total
	// request found is as expected
	d.GetUpdates(pkgSet)
	time.Sleep(srcRateLimit)
	d.GetUpdates(pkgSet)
	verifyReqCount(t, testSrcs, pkgSet, 3)

	pkgSetA := pkg.NewPackageSet()
	pkgSetA.Add(&emailPkg)
	pkgSetA.Add(&srchPkg)
	pkgSetB := pkg.NewPackageSet()
	pkgSetB.Add(&videoPkg)
	pkgSetB.Add(&srchPkg)
	go d.GetUpdates(pkgSetA)
	time.Sleep(srcRateLimit * 2)
	res := d.GetUpdates(pkgSetB)
	verifyReqCount(t, testSrcs, pkgSet, 4)
	verifyGetResults(t, pkgSetB, res)

	d.CancelAll()
}

func TestAddTUFSource(t *testing.T) {
	pkgSet := pkg.NewPackageSet()
	sources := []source.Source{}

	store, err := ioutil.TempDir("", "")
	if err != nil {
		t.Error(err)
	}
	defer os.RemoveAll(store)

	d, err := NewDaemon(store, pkgSet, processPackage, sources)
	if err != nil {
		t.Error(err)
	}

	actual_sources := d.GetSources()
	if len(actual_sources) != 0 {
		t.Errorf("Daemon started with non-zero sources")
	}

	blob_repos := d.blobRepos()
	if len(blob_repos) != 0 {
		t.Errorf("Daemon started with non-zero blob repos")
	}

	cfg1 := &amber.SourceConfig{
		Id:          "testing",
		RepoUrl:     "http://127.0.0.1:8083",
		BlobRepoUrl: "http://127.0.0.1:8083/blobs",
		RootKeys: []amber.KeyConfig{
			amber.KeyConfig{
				Type:  "ed25519",
				Value: "be0b983f7396da675c40c6b93e47fced7c1e9ea8a32a1fe952ba8f519760b307",
			},
		},
		StatusConfig: &amber.StatusConfig{Enabled: true},
	}
	d.AddTUFSource(cfg1)

	verifySourceConfigs(t, []*amber.SourceConfig{cfg1}, d.GetSources())
	verifyBlobRepos(t, []string{cfg1.BlobRepoUrl}, d.blobRepos())

	cfg2 := &amber.SourceConfig{
		Id:          "testing2",
		RepoUrl:     "http://127.0.0.2:8083",
		BlobRepoUrl: "http://127.0.0.2:8083/blobs",
		RootKeys: []amber.KeyConfig{
			amber.KeyConfig{
				Type:  "ed25519",
				Value: "be0b983f7396da675c40c6b93e47fced7c1e9ea8a32a1fe952ba8f519760b307",
			},
		},
		StatusConfig: &amber.StatusConfig{Enabled: true},
	}
	d.AddTUFSource(cfg2)

	verifySourceConfigs(t, []*amber.SourceConfig{cfg1, cfg2}, d.GetSources())
	verifyBlobRepos(t, []string{cfg1.BlobRepoUrl, cfg2.BlobRepoUrl}, d.blobRepos())

	d.CancelAll()
}

func TestRemoveTUFSource(t *testing.T) {
	pkgSet := pkg.NewPackageSet()
	sources := make([]source.Source, 0)

	store, err := ioutil.TempDir("", "")
	if err != nil {
		t.Error(err)
	}
	defer os.RemoveAll(store)

	d, err := NewDaemon(store, pkgSet, processPackage, sources)
	if err != nil {
		t.Error(err)
	}

	cfg1 := &amber.SourceConfig{
		Id:          "testing",
		RepoUrl:     "http://127.0.0.1:8083",
		BlobRepoUrl: "http://127.0.0.1:8083/blobs",
		RootKeys: []amber.KeyConfig{
			amber.KeyConfig{
				Type:  "ed25519",
				Value: "be0b983f7396da675c40c6b93e47fced7c1e9ea8a32a1fe952ba8f519760b307",
			},
		},
		StatusConfig: &amber.StatusConfig{Enabled: true},
	}
	cfg2 := &amber.SourceConfig{
		Id:          "testing2",
		RepoUrl:     "http://127.0.0.2:8083",
		BlobRepoUrl: "http://127.0.0.2:8083/blobs",
		RootKeys: []amber.KeyConfig{
			amber.KeyConfig{
				Type:  "ed25519",
				Value: "be0b983f7396da675c40c6b93e47fced7c1e9ea8a32a1fe952ba8f519760b307",
			},
		},
		StatusConfig: &amber.StatusConfig{Enabled: true},
	}
	d.AddTUFSource(cfg1)
	d.AddTUFSource(cfg2)

	verifySourceConfigs(t, []*amber.SourceConfig{cfg1, cfg2}, d.GetSources())
	verifyBlobRepos(t, []string{cfg1.BlobRepoUrl, cfg2.BlobRepoUrl}, d.blobRepos())
	verifySourceDirectories(t, []string{"testing", "testing2"}, store)

	status, err := d.RemoveTUFSource("testing2")

	if err != nil {
		t.Errorf("RemoveTUFSource returned an error: %v\n", err)
	}

	if status != amber.StatusOk {
		t.Errorf("RemoveTUFSource returned an unexpected status: %v\n", status)
	}

	verifySourceConfigs(t, []*amber.SourceConfig{cfg1}, d.GetSources())
	verifyBlobRepos(t, []string{cfg1.BlobRepoUrl}, d.blobRepos())
	verifySourceDirectories(t, []string{"testing"}, store)

	status, err = d.RemoveTUFSource("testing")

	if err != nil {
		t.Errorf("RemoveTUFSource returned an error: %v\n", err)
	}

	if status != amber.StatusOk {
		t.Errorf("RemoveTUFSource returned an unexpected status: %v\n", status)
	}

	verifySourceConfigs(t, []*amber.SourceConfig{}, d.GetSources())
	verifyBlobRepos(t, []string{}, d.blobRepos())
	verifySourceDirectories(t, []string{}, store)

	d.CancelAll()
}

func TestRemoveTUFSourceNonexisting(t *testing.T) {
	pkgSet := pkg.NewPackageSet()
	sources := make([]source.Source, 0)

	store, err := ioutil.TempDir("", "")
	if err != nil {
		t.Error(err)
	}
	defer os.RemoveAll(store)

	d, err := NewDaemon(store, pkgSet, processPackage, sources)
	if err != nil {
		t.Error(err)
	}

	verifySourceConfigs(t, []*amber.SourceConfig{}, d.GetSources())
	verifyBlobRepos(t, []string{}, d.blobRepos())
	verifySourceDirectories(t, []string{}, store)

	status, err := d.RemoveTUFSource("does_not_exist")

	if err != nil {
		t.Errorf("RemoveTUFSource returned an error: %v\n", err)
	}

	if status != amber.StatusErrNotFound {
		t.Errorf("RemoveTUFSource returned an unexpected status: %v\n", status)
	}

	verifySourceConfigs(t, []*amber.SourceConfig{}, d.GetSources())
	verifyBlobRepos(t, []string{}, d.blobRepos())
	verifySourceDirectories(t, []string{}, store)

	d.CancelAll()
}

func createMonitorPkgs() *pkg.PackageSet {
	pkgSet := pkg.NewPackageSet()
	pkgSet.Add(&pkg.Package{Name: "email", Version: "23af90ee"})
	pkgSet.Add(&pkg.Package{Name: "video", Version: "f2b8006c"})
	pkgSet.Add(&pkg.Package{Name: "search", Version: "fa08207e"})
	return pkgSet
}

func createTestSrcs() []*testSrc {
	pkgs := make(map[string]struct{})
	pkgs["email"] = struct{}{}
	pkgs["video"] = struct{}{}
	tSrc := testSrc{
		id:         "src",
		UpdateReqs: make(map[string]int),
		getReqs:    make(map[pkg.Package]*struct{}),
		interval:   time.Millisecond * 3,
		pkgs:       pkgs,
		limit:      1,
	}

	pkgs = make(map[string]struct{})
	pkgs["video"] = struct{}{}
	pkgs["search"] = struct{}{}
	tSrc2 := testSrc{
		id:         "src2",
		UpdateReqs: make(map[string]int),
		getReqs:    make(map[pkg.Package]*struct{}),
		interval:   time.Millisecond * 5,
		pkgs:       pkgs,
		limit:      1,
	}
	return []*testSrc{&tSrc, &tSrc2}
}

func verifyReqCount(t *testing.T, srcs []*testSrc, pkgs *pkg.PackageSet, runs int) {
	pkgChecks := make(map[pkg.Package]int)

	for _, pkg := range pkgs.Packages() {
		pkgChecks[*pkg] = 0

		for _, src := range srcs {
			pkgChecks[*pkg] += src.UpdateReqs[pkg.Name]
		}

		//actRuns := src.UpdateReqs[pkg.Name]
		if pkgChecks[*pkg] != runs {
			t.Errorf("Incorrect execution count, found %d, but expected %d for %s\n", pkgChecks[*pkg], runs, pkg.Name)
		}
	}

	for _, src := range srcs {
		if len(src.getReqs) != 0 {
			t.Errorf("Error, some pkgs were not requested!")
		}
	}
}

func verifyGetResults(t *testing.T, pkgSet *pkg.PackageSet,
	updates map[pkg.Package]*GetResult) {
	if len(updates) != len(pkgSet.Packages()) {
		t.Errorf("Expected %d updates, but found %d\n",
			len(pkgSet.Packages()), len(updates))
	}

	for _, p := range pkgSet.Packages() {
		r, ok := updates[*p]
		if !ok {
			t.Errorf("No result returned for package %q\n", p.Name)
		}

		if r.Err != nil {
			t.Errorf("Error finding update for package %q: %v",
				p.Name, r.Err)
		}

		if r.Orig.Name != p.Name || r.Orig.Version != p.Version {
			t.Errorf("Update result does not match original key, expected %q, but found %q", r.Orig.String(), p.String())
		}
	}
}

func verifySourceConfigs(t *testing.T, expected []*amber.SourceConfig, actual map[string]source.Source) {
	if len(expected) != len(actual) {
		t.Errorf("Expected %d sources, but found %d\n", len(expected), len(actual))
	}

	for _, cfg := range expected {
		src, ok := actual[cfg.Id]

		if !ok {
			t.Errorf("daemon has no source called %s\n", cfg.Id)
			continue
		}

		actual_config := src.GetConfig()
		if actual_config == nil {
			t.Errorf("source '%s' has nil config\n", cfg.Id)
			continue
		}

		if cfg != actual_config {
			t.Errorf("cfg != actual_config\n")
		}
	}
}

func verifyBlobRepos(t *testing.T, expected []string, blobs []BlobRepo) {
	if len(expected) != len(blobs) {
		t.Errorf("Expected %d blob repos, but found %d\n", len(expected), len(blobs))
	}

	actual := make([]string, 0, len(expected))

	for _, blob := range blobs {
		actual = append(actual, blob.Address)
	}

	sort.Strings(expected)
	sort.Strings(actual)

	if len(expected) != len(actual) {
		t.Errorf("Expected %d blob repos, but found %d\n", len(expected), len(actual))
	}
	for i, a := range expected {
		b := actual[i]
		if a != b {
			t.Errorf("Unexpected blob repo: %v != %v\n", expected, actual)
		}
	}
}

func verifySourceDirectories(t *testing.T, expected []string, store string) {
	entries, err := ioutil.ReadDir(store)
	if err != nil {
		t.Fatal("Error reading store", err)
	}

	actual := make([]string, 0)
	for _, entry := range entries {
		actual = append(actual, entry.Name())
	}
	if len(expected) != len(actual) {
		t.Errorf("Expected %d source directories, but found %d\n", len(expected), len(actual))
	}
	for i, a := range actual {
		b := expected[i]
		if a != b {
			t.Errorf("Unexpected source directory: %v != %v\n", expected, actual)
		}
		source_dir := filepath.Join(store, a)
		if _, err := os.Stat(filepath.Join(source_dir, "config.json")); err != nil {
			t.Errorf("Source directory has no config: %s\n", source_dir)
		}
	}
}

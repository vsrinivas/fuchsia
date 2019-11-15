// Copyright 2017 The Netstack Authors. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

package dns

import (
	"testing"
	"time"

	"golang.org/x/net/dns/dnsmessage"
)

const (
	example    = "example.com."
	fooExample = "foo.example.com."
	insertAll  = "insertAll"
	lookup     = "lookup"
	prune      = "prune"
)

func mustMakeResourceHeader(nameStr string, rhType dnsmessage.Type, ttl uint32) dnsmessage.ResourceHeader {
	name, err := dnsmessage.NewName(nameStr)
	if err != nil {
		panic(err)
	}
	return dnsmessage.ResourceHeader{
		Name:  name,
		Type:  rhType,
		Class: dnsmessage.ClassINET,
		TTL:   ttl,
	}
}

func makeTypeAResource(hName string, ttl uint32, A [4]byte) dnsmessage.Resource {
	return dnsmessage.Resource{
		Header: mustMakeResourceHeader(hName, dnsmessage.TypeA, ttl),
		Body: &dnsmessage.AResource{
			A,
		},
	}
}

func mustMakeCNAMEResource(hName, cName string, ttl uint32) dnsmessage.Resource {
	name, err := dnsmessage.NewName(cName)
	if err != nil {
		panic(err)
	}
	return dnsmessage.Resource{
		Header: mustMakeResourceHeader(hName, dnsmessage.TypeCNAME, ttl),
		Body: &dnsmessage.CNAMEResource{
			CNAME: name,
		},
	}
}

func makeSOAResource(hName string, ttl, minTTL uint32) dnsmessage.Resource {
	return dnsmessage.Resource{
		Header: mustMakeResourceHeader(hName, dnsmessage.TypeA, ttl),
		Body:   &dnsmessage.SOAResource{MinTTL: minTTL},
	}
}

func makeQuestion(nameStr string) dnsmessage.Question {
	name, err := dnsmessage.NewName(nameStr)
	if err != nil {
		panic(err)
	}
	return dnsmessage.Question{
		Name:  name,
		Type:  dnsmessage.TypeA,
		Class: dnsmessage.ClassINET,
	}
}

type checkParams struct {
	funcName     string
	gotResources []dnsmessage.Resource
	wantLen      int
	wantName     dnsmessage.Name
}

func check(t *testing.T, err error, params checkParams) {
	t.Helper()
	if err != nil {
		t.Errorf("After cache.%s, cache.lookup returned error: %v", params.funcName, err)
	}
	if len(params.gotResources) != params.wantLen {
		t.Errorf("cache.%s failed. Got Resource length %d. Want %d.", params.funcName, len(params.gotResources), params.wantLen)
	}
	for _, rr := range params.gotResources {
		if rr.Header.Name != params.wantName {
			t.Errorf("got cache.%s(...) = %v, want = %v", params.funcName, rr.Header.Name, params.wantName)
		}
	}
}

var (
	smallTestResources = []dnsmessage.Resource{
		makeTypeAResource(example, 5, [4]byte{127, 0, 0, 1}),
		makeTypeAResource(example, 5, [4]byte{127, 0, 0, 2}),
	}
	soaAuthority       = makeSOAResource(example, 5, 12)
	exampleQuestion    = makeQuestion(example)
	fooExampleQuestion = makeQuestion(fooExample)
)

// Tests a simple insert and lookup pair.
func TestLookup(t *testing.T) {
	cache := newCache()
	cache.insertAll(smallTestResources)
	visited := make(map[dnsmessage.Name]struct{})
	rrs, err := cache.lookup(exampleQuestion, visited, 0)
	check(t, err, checkParams{
		funcName:     lookup,
		gotResources: rrs,
		wantLen:      2,
		wantName:     exampleQuestion.Name,
	})
}

// Tests that entries are pruned when they expire, and not before.
func TestExpires(t *testing.T) {
	cache := newCache()

	// These records expire at 5 seconds.
	testTime := time.Now()
	origTimeNow := timeNow
	defer func() { timeNow = origTimeNow }()
	timeNow = func() time.Time { return testTime }
	cache.insertAll(smallTestResources)

	// Still there after t=4 seconds.
	testTime = testTime.Add(4 * time.Second)
	cache.prune()
	{
		visited := make(map[dnsmessage.Name]struct{})
		rrs, err := cache.lookup(exampleQuestion, visited, 0)
		check(t, err, checkParams{
			funcName:     prune,
			gotResources: rrs,
			wantLen:      2,
			wantName:     exampleQuestion.Name,
		})
	}

	// Gone after t=6 seconds.
	testTime = testTime.Add(2 * time.Second)
	cache.prune()
	{
		visited := make(map[dnsmessage.Name]struct{})
		rrs, err := cache.lookup(exampleQuestion, visited, 0)
		check(t, err, checkParams{
			funcName:     prune,
			gotResources: rrs,
			wantLen:      0,
			wantName:     exampleQuestion.Name,
		})
	}
}

// Tests that a Resource Record with the name of an existing CNAMERecord is inserted and the existing CNAMERecord is overwritten.
func TestInsertWithExistingCNAMEResource(t *testing.T) {
	cache := newCache()
	cache.insertAll(smallTestResources)
	cache.insertAll([]dnsmessage.Resource{mustMakeCNAMEResource(fooExample, example, 5)})
	{
		visited := make(map[dnsmessage.Name]struct{})
		rrs, err := cache.lookup(fooExampleQuestion, visited, 0)
		check(t, err, checkParams{
			funcName:     lookup,
			gotResources: rrs,
			wantLen:      2,
			wantName:     exampleQuestion.Name,
		})
	}

	// Insert a CNAMEResource with the name of an existing CNAMEResource.
	anotherExample := "anotherExample"
	cache.insertAll([]dnsmessage.Resource{makeTypeAResource(anotherExample, 6, [4]byte{127, 0, 0, 3}), mustMakeCNAMEResource(fooExample, anotherExample, 5)})
	{
		visited := make(map[dnsmessage.Name]struct{})
		name, err := dnsmessage.NewName(anotherExample)
		if err != nil {
			t.Fatalf("dnsmessage.NewName(%s) failed with error :%s", anotherExample, err)
		}
		rrs, err := cache.lookup(fooExampleQuestion, visited, 0)
		check(t, err, checkParams{
			funcName:     insertAll,
			gotResources: rrs,
			wantLen:      1,
			wantName:     name,
		})
	}

	// Insert a TypeAResource with the name of an existing CNAMEResource.
	cache.insertAll([]dnsmessage.Resource{makeTypeAResource(fooExample, 5, [4]byte{127, 0, 0, 4})})
	{
		visited := make(map[dnsmessage.Name]struct{})
		rrs, err := cache.lookup(fooExampleQuestion, visited, 0)
		check(t, err, checkParams{
			funcName:     insertAll,
			gotResources: rrs,
			wantLen:      1,
			wantName:     fooExampleQuestion.Name,
		})
	}
}

// Tests that a CNAMEResource with the name of existing Resources Records is inserted and the existing Resource Records are overwritten.
func TestInsertCNAMEResourceWithExistingTypeAResources(t *testing.T) {
	cache := newCache()
	cache.insertAll(smallTestResources)
	{
		visited := make(map[dnsmessage.Name]struct{})
		rrs, err := cache.lookup(exampleQuestion, visited, 0)
		check(t, err, checkParams{
			funcName:     lookup,
			gotResources: rrs,
			wantLen:      2,
			wantName:     exampleQuestion.Name,
		})
	}

	// Insert a CNAMEResource with the name of existing TypeAResource and TypeAResources are overwritten.
	cache.insertAll([]dnsmessage.Resource{mustMakeCNAMEResource(example, fooExample, 5)})
	{
		visited := make(map[dnsmessage.Name]struct{})
		rrs, err := cache.lookup(exampleQuestion, visited, 0)
		check(t, err, checkParams{
			funcName:     insertAll,
			gotResources: rrs,
			wantLen:      0,
			wantName:     exampleQuestion.Name,
		})
	}
}

// Tests that we can't insert more than maxEntries entries, but after pruning old ones, we can insert again.
func TestMaxEntries(t *testing.T) {
	cache := newCache()

	testTime := time.Now()
	origTimeNow := timeNow
	defer func() { timeNow = origTimeNow }()
	timeNow = func() time.Time { return testTime }

	// One record that expires at 10 seconds.
	cache.insertAll([]dnsmessage.Resource{
		makeTypeAResource(example, 10, [4]byte{127, 0, 0, 1}),
	})

	// A bunch that expire at 5 seconds.
	for i := 0; i < maxEntries; i++ {
		cache.insertAll([]dnsmessage.Resource{
			makeTypeAResource(example, 5, [4]byte{byte(i >> 24), byte(i >> 16), byte(i >> 8), byte(i)}),
		})
	}

	{
		visited := make(map[dnsmessage.Name]struct{})
		rrs, err := cache.lookup(exampleQuestion, visited, 0)
		check(t, err, checkParams{
			funcName:     insertAll,
			gotResources: rrs,
			wantLen:      maxEntries,
			wantName:     exampleQuestion.Name,
		})
	}
	// Cache is at capacity. Can't insert anymore.
	cache.insertAll([]dnsmessage.Resource{
		makeTypeAResource(fooExample, 5, [4]byte{192, 168, 0, 1}),
	})
	{
		visited := make(map[dnsmessage.Name]struct{})
		rrs, err := cache.lookup(fooExampleQuestion, visited, 0)
		check(t, err, checkParams{
			funcName:     insertAll,
			gotResources: rrs,
			wantLen:      0,
			wantName:     exampleQuestion.Name,
		})
	}

	// Advance the clock so the 5 second entries expire. Insert should succeed.
	testTime = testTime.Add(6 * time.Second)
	cache.insertAll([]dnsmessage.Resource{
		makeTypeAResource(fooExample, 5, [4]byte{192, 168, 0, 1}),
	})
	{
		visited := make(map[dnsmessage.Name]struct{})
		rrs, err := cache.lookup(fooExampleQuestion, visited, 0)
		check(t, err, checkParams{
			funcName:     insertAll,
			gotResources: rrs,
			wantLen:      1,
			wantName:     fooExampleQuestion.Name,
		})
	}

}

// Tests that we get results when looking up a domain alias.
func TestCNAME(t *testing.T) {
	cache := newCache()
	cache.insertAll(smallTestResources)

	// One CNAME record that points at an existing record.
	cache.insertAll([]dnsmessage.Resource{
		mustMakeCNAMEResource(fooExample, example, 10),
	})

	visited := make(map[dnsmessage.Name]struct{})
	rrs, err := cache.lookup(fooExampleQuestion, visited, 0)
	check(t, err, checkParams{
		funcName:     lookup,
		gotResources: rrs,
		wantLen:      2,
		wantName:     exampleQuestion.Name,
	})
}

// Tests that there is a loop in the CNAME aliases.
func TestCNAMELoop(t *testing.T) {
	cache := newCache()
	cache.insertAll(smallTestResources)
	cache.insertAll([]dnsmessage.Resource{mustMakeCNAMEResource(fooExample, example, 5)})
	{
		visited := make(map[dnsmessage.Name]struct{})
		rrs, err := cache.lookup(fooExampleQuestion, visited, 0)
		check(t, err, checkParams{
			funcName:     lookup,
			gotResources: rrs,
			wantLen:      2,
			wantName:     exampleQuestion.Name,
		})
	}

	// Form a loop of CNAME.
	cache.insertAll([]dnsmessage.Resource{mustMakeCNAMEResource(example, fooExample, 5)})
	{
		visited := make(map[dnsmessage.Name]struct{})
		if _, err := cache.lookup(fooExampleQuestion, visited, 0); err != errCNAMELoop {
			t.Errorf("cache.lookup got an unexpected error. Got %v, Want %v", err, errCNAMELoop)
		}
	}
}

// Tests that the level of CNAMEResource exceeds maxCNAMELevel.
func TestCNAMELevel(t *testing.T) {
	cache := newCache()
	name := 'z'
	cache.insertAll(smallTestResources)
	cache.insertAll([]dnsmessage.Resource{mustMakeCNAMEResource(string(name), example, 5)})
	for i := 0; i < maxCNAMELevel-1; i++ {
		cache.insertAll([]dnsmessage.Resource{mustMakeCNAMEResource(string(name-1), string(name), 5)})
		name--
	}
	{
		visited := make(map[dnsmessage.Name]struct{})
		rrs, err := cache.lookup(makeQuestion(string(name)), visited, 0)
		check(t, err, checkParams{
			funcName:     lookup,
			gotResources: rrs,
			wantLen:      2,
			wantName:     exampleQuestion.Name,
		})
	}
	// Add one more level of CNAME aliases.
	cache.insertAll([]dnsmessage.Resource{mustMakeCNAMEResource(string(name-1), string(name), 5)})
	name--
	{
		visited := make(map[dnsmessage.Name]struct{})
		if _, err := cache.lookup(makeQuestion(string(name)), visited, 0); err != errCNAMELevel {
			t.Errorf("cache.lookup got an unexpected error. Got %v, Want %v", err, errCNAMELevel)
		}
	}
}

// Tests that duplicate CNAMEResoures aren't allowed, so that no duplicate {A,AAAA}Resource will be returned.
func TestDupeCNAME(t *testing.T) {
	cache := newCache()
	cache.insertAll(smallTestResources)
	{
		visited := make(map[dnsmessage.Name]struct{})
		rrs, err := cache.lookup(exampleQuestion, visited, 0)
		check(t, err, checkParams{
			funcName:     lookup,
			gotResources: rrs,
			wantLen:      2,
			wantName:     exampleQuestion.Name,
		})
	}

	cache.insertAll([]dnsmessage.Resource{
		mustMakeCNAMEResource(fooExample, example, 5),
	})
	{
		visited := make(map[dnsmessage.Name]struct{})
		rrs, err := cache.lookup(fooExampleQuestion, visited, 0)
		check(t, err, checkParams{
			funcName:     lookup,
			gotResources: rrs,
			wantLen:      2,
			wantName:     exampleQuestion.Name,
		})
	}

	// Insert fooExample CNAME example again with different ttl.
	cache.insertAll([]dnsmessage.Resource{
		mustMakeCNAMEResource(fooExample, example, 10),
	})
	{
		visited := make(map[dnsmessage.Name]struct{})
		rrs, err := cache.lookup(fooExampleQuestion, visited, 0)
		check(t, err, checkParams{
			funcName:     lookup,
			gotResources: rrs,
			wantLen:      2,
			wantName:     exampleQuestion.Name,
		})
	}
}

// Tests that the cache doesn't store multiple identical AResource.
func TestDupeAResource(t *testing.T) {
	cache := newCache()
	cache.insertAll(smallTestResources)
	cache.insertAll(smallTestResources)
	visited := make(map[dnsmessage.Name]struct{})
	rrs, err := cache.lookup(exampleQuestion, visited, 0)
	check(t, err, checkParams{
		funcName:     lookup,
		gotResources: rrs,
		wantLen:      2,
		wantName:     exampleQuestion.Name,
	})
}

// Tests that we can insert and expire negative resources.
func TestNegative(t *testing.T) {
	cache := newCache()

	// The negative record expires at 12 seconds (taken from the SOA authority resource).
	testTime := time.Now()
	origTimeNow := timeNow
	defer func() { timeNow = origTimeNow }()
	timeNow = func() time.Time { return testTime }

	cache.insertNegative(exampleQuestion, dnsmessage.Message{
		Questions:   []dnsmessage.Question{exampleQuestion},
		Authorities: []dnsmessage.Resource{soaAuthority},
	})

	// Still there after t=11 seconds.
	testTime = testTime.Add(11 * time.Second)
	cache.prune()
	{
		visited := make(map[dnsmessage.Name]struct{})
		rrs, err := cache.lookup(exampleQuestion, visited, 0)
		check(t, err, checkParams{
			funcName:     prune,
			gotResources: rrs,
			wantLen:      1,
			wantName:     exampleQuestion.Name,
		})
	}

	// Gone after t=13 seconds.
	testTime = testTime.Add(2 * time.Second)
	cache.prune()
	{
		visited := make(map[dnsmessage.Name]struct{})
		rrs, err := cache.lookup(exampleQuestion, visited, 0)
		check(t, err, checkParams{
			funcName:     prune,
			gotResources: rrs,
			wantLen:      0,
			wantName:     exampleQuestion.Name,
		})
	}
}

// Tests that a negative resource is replaced when we have an actual resource for that query.
func TestNegativeUpdate(t *testing.T) {
	cache := newCache()
	cache.insertNegative(exampleQuestion, dnsmessage.Message{
		Questions:   []dnsmessage.Question{exampleQuestion},
		Authorities: []dnsmessage.Resource{soaAuthority},
	})
	cache.insertAll(smallTestResources)
	visited := make(map[dnsmessage.Name]struct{})
	rrs, err := cache.lookup(exampleQuestion, visited, 0)
	check(t, err, checkParams{
		funcName:     lookup,
		gotResources: rrs,
		wantLen:      2,
		wantName:     exampleQuestion.Name,
	})
}

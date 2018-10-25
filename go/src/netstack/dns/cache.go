// Copyright 2017 The Netstack Authors. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

package dns

import (
	"log"
	"math"
	"sync"
	"time"

	"netstack/dns/dnsmessage"
)

const (
	// TODO: Think about a good value. dnsmasq defaults to 150 names.
	maxEntries = 1024
)

const debug = false

var testHookNow = func() time.Time { return time.Now() }

// Single entry in the cache, like a TypeA resource holding an IPv4 address.
type cacheEntry struct {
	rr  dnsmessage.Resource // the resource
	ttd time.Time           // when this entry expires
}

// Returns true if this entry is a CNAME that points at something no longer in our cache.
func (entry *cacheEntry) isDanglingCNAME(cache *cacheInfo) bool {
	switch rr := entry.rr.(type) {
	case *dnsmessage.CNAMEResource:
		return cache.m[rr.CNAME] == nil
	default:
		return false
	}
}

// The full cache.
type cacheInfo struct {
	mu         sync.Mutex
	m          map[string][]cacheEntry
	numEntries int
}

func newCache() cacheInfo {
	return cacheInfo{m: make(map[string][]cacheEntry)}
}

// Returns a list of Resources that match the given Question (same class and type and matching domain name).
func (cache *cacheInfo) lookup(question *dnsmessage.Question) []dnsmessage.Resource {
	entries := cache.m[question.Name]

	rrs := make([]dnsmessage.Resource, 0, len(entries))
	for _, entry := range entries {
		h := entry.rr.Header()
		if h.Class == question.Class && h.Name == question.Name {
			switch rr := entry.rr.(type) {
			case *dnsmessage.CNAMEResource:
				rrs = append(rrs, cache.lookup(&dnsmessage.Question{
					Name:  rr.CNAME,
					Class: question.Class,
					Type:  question.Type,
				})...)
			default:
				if h.Type == question.Type {
					rrs = append(rrs, rr)
				}
			}
		}
	}
	return rrs
}

func resourceEqual(r1 dnsmessage.Resource, r2 dnsmessage.Resource) bool {
	h1 := r1.Header()
	h2 := r2.Header()
	if h1.Class != h2.Class || h1.Type != h2.Type || h1.Name != h2.Name {
		return false
	}
	switch r1 := r1.(type) {
	case *dnsmessage.AResource:
		return r1.A == r2.(*dnsmessage.AResource).A
	case *dnsmessage.AAAAResource:
		return r1.AAAA == r2.(*dnsmessage.AAAAResource).AAAA
	case *dnsmessage.CNAMEResource:
		return r1.CNAME == r2.(*dnsmessage.CNAMEResource).CNAME
	case *dnsmessage.NegativeResource:
		return true
	}
	panic("unexpected resource type")
}

// Searches `entries` for an exact resource match, returning the entry if found.
func findExact(entries []cacheEntry, rr dnsmessage.Resource) *cacheEntry {
	for i, entry := range entries {
		if resourceEqual(entry.rr, rr) {
			return &entries[i]
		}
	}
	return nil
}

// Finds the minimum TTL value of any SOA resource in a response. Returns 0 if not found.
// This is used for caching a failed DNS query. See RFC 2308.
func findSOAMinTTL(auths []dnsmessage.Resource) uint32 {
	minTTL := uint32(math.MaxUint32)
	foundSOA := false
	for _, auth := range auths {
		if auth.Header().Class == dnsmessage.ClassINET {
			switch soa := auth.(type) {
			case *dnsmessage.SOAResource:
				foundSOA = true
				if soa.MinTTL < minTTL {
					minTTL = soa.MinTTL
				}
			}
		}
	}
	if foundSOA {
		return minTTL
	}
	return 0
}

// Attempts to add a new entry into the cache. Can fail if the cache is full.
func (cache *cacheInfo) insert(rr dnsmessage.Resource) {
	h := rr.Header()
	newEntry := cacheEntry{
		ttd: testHookNow().Add(time.Duration(h.TTL) * time.Second),
		rr:  rr,
	}

	entries := cache.m[h.Name]
	if existing := findExact(entries, rr); existing != nil {
		if _, ok := existing.rr.(*dnsmessage.NegativeResource); ok {
			// We have a valid record now; replace the negative resource entirely.
			existing.rr = rr
			existing.ttd = newEntry.ttd
		} else if newEntry.ttd.After(existing.ttd) {
			existing.ttd = newEntry.ttd
		}
		if debug {
			log.Printf("DNS cache update: %v(%v) expires %v", h.Name, h.Type, existing.ttd)
		}
	} else if cache.numEntries+1 <= maxEntries {
		if debug {
			log.Printf("DNS cache insert: %v(%v) expires %v", h.Name, h.Type, newEntry.ttd)
		}
		cache.m[h.Name] = append(entries, newEntry)
		cache.numEntries++
	} else {
		// TODO(mpcomplete): might be better to evict the LRU entry instead.
		// TODO(mpcomplete): RFC 1035 7.4 says that if we can't cache this RR, we
		// shouldn't cache any other RRs for the same name in this response.
		log.Printf("DNS cache is full; insert failed: %v(%v)", h.Name, h.Type)
	}
}

// Attempts to add each Resource as a new entry in the cache. Can fail if the cache is full.
func (cache *cacheInfo) insertAll(rrs []dnsmessage.Resource) {
	cache.prune()
	for _, rr := range rrs {
		h := rr.Header()
		if h.Class == dnsmessage.ClassINET {
			switch h.Type {
			case dnsmessage.TypeA, dnsmessage.TypeAAAA, dnsmessage.TypeCNAME:
				cache.insert(rr)
			}
		}
	}
}

func (cache *cacheInfo) insertNegative(question *dnsmessage.Question, msg *dnsmessage.Message) {
	cache.prune()
	minTTL := findSOAMinTTL(msg.Authorities)
	if minTTL == 0 {
		// Don't cache without a TTL value.
		return
	}
	rr := &dnsmessage.NegativeResource{
		ResourceHeader: dnsmessage.ResourceHeader{
			Name:  question.Name,
			Type:  question.Type,
			Class: dnsmessage.ClassINET,
			TTL:   minTTL,
		},
	}
	cache.insert(rr)
}

// Removes every expired/dangling entry from the cache.
func (cache *cacheInfo) prune() {
	now := testHookNow()
	for name, entries := range cache.m {
		removed := false
		for i := 0; i < len(entries); {
			if now.After(entries[i].ttd) || entries[i].isDanglingCNAME(cache) {
				entries[i] = entries[len(entries)-1]
				entries = entries[:len(entries)-1]
				cache.numEntries--
				removed = true
			} else {
				i++
			}
		}
		if len(entries) == 0 {
			delete(cache.m, name)
		} else if removed {
			cache.m[name] = entries
		}
	}
}

var cache = newCache()

func newCachedResolver(fallback Resolver) Resolver {
	return func(c *Client, question dnsmessage.Question) (string, []dnsmessage.Resource, *dnsmessage.Message, error) {
		if !(question.Class == dnsmessage.ClassINET && (question.Type == dnsmessage.TypeA || question.Type == dnsmessage.TypeAAAA)) {
			panic("unexpected question type")
		}

		cache.mu.Lock()
		rrs := cache.lookup(&question)
		cache.mu.Unlock()
		if len(rrs) != 0 {
			if debug {
				log.Printf("DNS cache hit %v(%v) => %v", question.Name, question.Type, rrs)
			}
			return "", rrs, nil, nil
		}

		cname, rrs, msg, err := fallback(c, question)
		if debug {
			log.Printf("DNS cache miss, server returned %v(%v) => %v; err=%v", question.Name, question.Type, rrs, err)
		}
		cache.mu.Lock()
		if err == nil {
			cache.insertAll(msg.Answers)
		} else if err, ok := err.(*Error); ok && err.CacheNegative {
			cache.insertNegative(&question, msg)
		}
		cache.mu.Unlock()

		return cname, rrs, msg, err
	}
}

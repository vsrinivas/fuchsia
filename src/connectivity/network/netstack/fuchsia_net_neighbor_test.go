// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package netstack

import (
	"context"
	"testing"

	"fidl/fuchsia/net/neighbor"

	"github.com/google/go-cmp/cmp"
)

// TestNeighborEntryIteratorGetNext verifies correct batching behavior and
// ordering of entries when receiving entries via EntryIterator.GetNext.
func TestNeighborEntryIteratorGetNext(t *testing.T) {
	tests := []struct {
		name              string
		existingItemCount uint64
		batchSizes        []uint64
	}{
		// An idle item will always be sent, even if there are no existing entries.
		{
			name:              "One",
			existingItemCount: 0,
			batchSizes:        []uint64{1},
		},
		// A full batch consistents of existing items and one idle item.
		{
			name:              "Max",
			existingItemCount: neighbor.MaxItemBatchSize - 1,
			batchSizes: []uint64{
				neighbor.MaxItemBatchSize,
			},
		},
		// Another batch is needed to send the extra idle item.
		{
			name:              "OverMax",
			existingItemCount: neighbor.MaxItemBatchSize,
			batchSizes: []uint64{
				neighbor.MaxItemBatchSize,
				1,
			},
		},
		{
			name:              "OverMaxTwice",
			existingItemCount: neighbor.MaxItemBatchSize * 2,
			batchSizes: []uint64{
				neighbor.MaxItemBatchSize,
				neighbor.MaxItemBatchSize,
				1,
			},
		},
	}

	for _, test := range tests {
		t.Run(test.name, func(t *testing.T) {
			var wantItems []neighbor.EntryIteratorItem

			// Fill the list with existing items, then append the idle item to
			// indicate the end of existing items.
			for i := uint64(0); i < test.existingItemCount; i++ {
				entry := neighbor.Entry{}
				// Repurpose the interface field as a sequence number, used later to
				// ensure the ordering of entries.
				entry.SetInterface(i)
				wantItems = append(wantItems, neighbor.EntryIteratorItemWithExisting(entry))
			}
			wantItems = append(wantItems, neighbor.EntryIteratorItemWithIdle(neighbor.IdleEvent{}))

			it := neighborEntryIterator{
				items: wantItems,
			}

			var gotItems []neighbor.EntryIteratorItem

			// Check item batch sizes and collect all items received.
			for _, want := range test.batchSizes {
				entries, err := it.GetNext(context.Background())
				if err != nil {
					t.Errorf("it.GetNext(_): %s", err)
				}
				if got := uint64(len(entries)); got != want {
					t.Errorf("got len(it.GetNext(_)) = %d, want = %d", got, want)
				}
				gotItems = append(gotItems, entries...)
			}

			// Ensure ordering of items by diffing the interface field.
			if diff := cmp.Diff(wantItems, gotItems); diff != "" {
				t.Fatalf("item mismatch (-want +got):\n%s", diff)
			}
		})
	}
}

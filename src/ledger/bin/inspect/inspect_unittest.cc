// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/inspect/inspect.h"

#include <string>

#include "gtest/gtest.h"
#include "src/ledger/bin/app/constants.h"
#include "src/ledger/bin/storage/public/types.h"

namespace ledger {
namespace {

TEST(Inspect, PageIdToDisplayName) {
  EXPECT_EQ("00000000000000000000000000000000",
            PageIdToDisplayName(storage::PageId(kRootPageId.ToString())));

  // Taken from a real Ledger-using component!
  EXPECT_EQ("4D657373616765517565756550616765 (\"MessageQueuePage\")",
            PageIdToDisplayName(storage::PageId("MessageQueuePage")));

  // Taken from a real Ledger-using component!
  EXPECT_EQ("436C6970626F617264506167655F5F5F (\"ClipboardPage___\")",
            PageIdToDisplayName(
                storage::PageId(storage::PageId("ClipboardPage___"))));

  // Taken from a real Ledger-using component... that was using Ledger's
  // generate-a-random-page-id feature.
  EXPECT_EQ(
      "B69F65D45A28ADF74195748C2548EAF3",
      PageIdToDisplayName(storage::PageId(
          "\xB6\x9F\x65\xD4\x5A\x28\xAD\xF7\x41\x95\x74\x8C\x25\x48\xEA\xF3")));
}

TEST(Inspect, PageDisplayNameToPageId) {
  storage::PageId root_page_id;
  EXPECT_TRUE(PageDisplayNameToPageId("00000000000000000000000000000000",
                                      &root_page_id));
  EXPECT_EQ(kRootPageId, root_page_id);

  // Taken from a real Ledger-using component!
  storage::PageId message_queue_page_page_id;
  EXPECT_TRUE(PageDisplayNameToPageId(
      "4D657373616765517565756550616765 (\"MessageQueuePage\")",
      &message_queue_page_page_id));
  EXPECT_EQ(storage::PageId("MessageQueuePage"), message_queue_page_page_id);

  // Taken from a real Ledger-using component!
  storage::PageId clipboard_page_page_id;
  EXPECT_TRUE(PageDisplayNameToPageId(
      "436C6970626F617264506167655F5F5F (\"ClipboardPage___\")",
      &clipboard_page_page_id));
  EXPECT_EQ(storage::PageId("ClipboardPage___"), clipboard_page_page_id);

  // Taken from a real Ledger-using component... that was using Ledger's
  // generate-a-random-page-id feature.
  storage::PageId random_page_id;
  EXPECT_TRUE(PageDisplayNameToPageId("B69F65D45A28ADF74195748C2548EAF3",
                                      &random_page_id));
  EXPECT_EQ(
      storage::PageId(
          "\xB6\x9F\x65\xD4\x5A\x28\xAD\xF7\x41\x95\x74\x8C\x25\x48\xEA\xF3"),
      random_page_id);

  storage::PageId zero_length_page_id;
  EXPECT_FALSE(PageDisplayNameToPageId("", &zero_length_page_id));

  storage::PageId too_short_page_id;
  EXPECT_FALSE(PageDisplayNameToPageId("434D59", &too_short_page_id));

  storage::PageId malformed_page_id;
  EXPECT_FALSE(PageDisplayNameToPageId(
      "436C6970626F617264506167655F5F (\"ClipboardPage__\")",
      &malformed_page_id));
}

}  // namespace
}  // namespace ledger

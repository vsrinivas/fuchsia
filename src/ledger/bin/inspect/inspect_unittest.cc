// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/inspect/inspect.h"

#include <string>

#include "gtest/gtest.h"
#include "src/ledger/bin/app/constants.h"
#include "src/ledger/bin/storage/public/constants.h"
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
            PageIdToDisplayName(storage::PageId(storage::PageId("ClipboardPage___"))));

  // Taken from a real Ledger-using component... that was using Ledger's
  // generate-a-random-page-id feature.
  EXPECT_EQ("B69F65D45A28ADF74195748C2548EAF3",
            PageIdToDisplayName(storage::PageId(
                "\xB6\x9F\x65\xD4\x5A\x28\xAD\xF7\x41\x95\x74\x8C\x25\x48\xEA\xF3")));
}

TEST(Inspect, PageDisplayNameToPageId) {
  storage::PageId root_page_id;
  EXPECT_TRUE(PageDisplayNameToPageId("00000000000000000000000000000000", &root_page_id));
  EXPECT_EQ(kRootPageId, root_page_id);

  // Taken from a real Ledger-using component!
  storage::PageId message_queue_page_page_id;
  EXPECT_TRUE(PageDisplayNameToPageId("4D657373616765517565756550616765 (\"MessageQueuePage\")",
                                      &message_queue_page_page_id));
  EXPECT_EQ(storage::PageId("MessageQueuePage"), message_queue_page_page_id);

  // Taken from a real Ledger-using component!
  storage::PageId clipboard_page_page_id;
  EXPECT_TRUE(PageDisplayNameToPageId("436C6970626F617264506167655F5F5F (\"ClipboardPage___\")",
                                      &clipboard_page_page_id));
  EXPECT_EQ(storage::PageId("ClipboardPage___"), clipboard_page_page_id);

  // Taken from a real Ledger-using component... that was using Ledger's
  // generate-a-random-page-id feature.
  storage::PageId random_page_id;
  EXPECT_TRUE(PageDisplayNameToPageId("B69F65D45A28ADF74195748C2548EAF3", &random_page_id));
  EXPECT_EQ(storage::PageId("\xB6\x9F\x65\xD4\x5A\x28\xAD\xF7\x41\x95\x74\x8C\x25\x48\xEA\xF3"),
            random_page_id);

  storage::PageId zero_length_page_id;
  EXPECT_FALSE(PageDisplayNameToPageId("", &zero_length_page_id));

  storage::PageId too_short_page_id;
  EXPECT_FALSE(PageDisplayNameToPageId("434D59", &too_short_page_id));

  storage::PageId malformed_page_id;
  EXPECT_FALSE(PageDisplayNameToPageId("436C6970626F617264506167655F5F (\"ClipboardPage__\")",
                                       &malformed_page_id));
}

TEST(Inspect, CommitIdToDisplayName) {
  EXPECT_EQ("0000000000000000000000000000000000000000000000000000000000000000",
            CommitIdToDisplayName(storage::CommitId(
                "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0",
                storage::kCommitIdSize)));

  EXPECT_EQ("16D25ABC4023C7198F228C19CA4EBF5C7D78D4C1868EA5891DAC1541092D1EFE",
            CommitIdToDisplayName(storage::CommitId(
                "\x16\xD2\x5A\xBC\x40\x23\xC7\x19\x8F\x22\x8C\x19\xCA\x4E\xBF\x5C\x7D"
                "\x78\xD4\xC1\x86\x8E\xA5\x89\x1D\xAC\x15\x41\x09\x2D\x1E\xFE")));
}

TEST(Inspect, CommitDisplayNameToCommitId) {
  storage::CommitId root_commit_id;
  EXPECT_TRUE(CommitDisplayNameToCommitId(
      "0000000000000000000000000000000000000000000000000000000000000000", &root_commit_id));
  EXPECT_EQ(storage::CommitId("\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0",
                              storage::kCommitIdSize),
            root_commit_id);

  storage::CommitId nonzero_commit_id;
  EXPECT_TRUE(CommitDisplayNameToCommitId(
      "16D25ABC4023C7198F228C19CA4EBF5C7D78D4C1868EA5891DAC1541092D1EFE", &nonzero_commit_id));
  EXPECT_EQ(storage::CommitId("\x16\xD2\x5A\xBC\x40\x23\xC7\x19\x8F\x22\x8C\x19\xCA\x4E\xBF\x5C\x7D"
                              "\x78\xD4\xC1\x86\x8E\xA5\x89\x1D\xAC\x15\x41\x09\x2D\x1E\xFE"),
            nonzero_commit_id);

  storage::CommitId zero_length_commmit_id;
  EXPECT_FALSE(CommitDisplayNameToCommitId("", &zero_length_commmit_id));

  storage::CommitId too_short_commit_id;
  EXPECT_FALSE(CommitDisplayNameToCommitId("475842", &too_short_commit_id));

  storage::CommitId malformed_long_commit_id;
  EXPECT_FALSE(CommitDisplayNameToCommitId(
      "16D25ABC4023C7198F228C19CA here's some content that comes from nowhere",
      &malformed_long_commit_id));

  storage::CommitId malformed_commit_id_of_expected_length;
  EXPECT_FALSE(CommitDisplayNameToCommitId(
      "?#D25ABC402>C71.8UF!28C19CA*4EBF5C7D78D4C1, 868EA589/1DAC1Q541t@",
      &malformed_commit_id_of_expected_length));
}

TEST(Inspect, KeyToDisplayName) {
  EXPECT_EQ("(\"\") ", KeyToDisplayName(""));

  EXPECT_EQ(std::string(34, '0'), KeyToDisplayName(std::string(17, '\0')));

  EXPECT_EQ("(\"Nuage\") 4E75616765", KeyToDisplayName("Nuage"));

  EXPECT_EQ(std::string(kMaxKeySize * 2, '4'), KeyToDisplayName(std::string(kMaxKeySize, 'D')));

  // Taken from a real Ledger-using component!
  EXPECT_EQ(
      "(\"Module/nathaniel_todo_list\") "
      "4D6F64756C652F6E617468616E69656C5F746F646F5F6C697374",
      KeyToDisplayName("Module/nathaniel_todo_list"));

  // Taken from a real Ledger-using component! ...but seems random and of no
  // particular significance.
  EXPECT_EQ("357C2814B45F1E83D463624E75F659B6",
            KeyToDisplayName("\x35\x7C\x28\x14\xB4\x5F\x1E\x83\xD4\x63\x62\x4E\x75\xF6\x59\xB6"));
}

TEST(Inspect, KeyDisplayNameToKey) {
  std::string zero_length_key;
  EXPECT_TRUE(KeyDisplayNameToKey("", &zero_length_key));
  EXPECT_EQ("", zero_length_key);

  std::string all_zeros_key;
  EXPECT_TRUE(KeyDisplayNameToKey(std::string(kMaxKeySize * 2, '0'), &all_zeros_key));
  EXPECT_EQ(std::string(kMaxKeySize, '\0'), all_zeros_key);

  std::string max_size_key;
  EXPECT_TRUE(KeyDisplayNameToKey(std::string(kMaxKeySize * 2, '5'), &max_size_key));
  EXPECT_EQ(std::string(kMaxKeySize, 'U'), max_size_key);

  // Taken from a real Ledger-using component!
  std::string module_nathaniel_todo_list;
  EXPECT_TRUE(
      KeyDisplayNameToKey("(\"Module/nathaniel_todo_list\") "
                          "4D6F64756C652F6E617468616E69656C5F746F646F5F6C697374",
                          &module_nathaniel_todo_list));
  EXPECT_EQ("Module/nathaniel_todo_list", module_nathaniel_todo_list);

  // Taken from a real Ledger-using component! ...but seems random and of no
  // particular significance.
  std::string arbitrary_key;
  EXPECT_TRUE(KeyDisplayNameToKey("357C2814B45F1E83D463624E75F659B6", &arbitrary_key));
  EXPECT_EQ("\x35\x7C\x28\x14\xB4\x5F\x1E\x83\xD4\x63\x62\x4E\x75\xF6\x59\xB6", arbitrary_key);

  std::string illegal_length_key;
  EXPECT_FALSE(KeyDisplayNameToKey("A", &illegal_length_key));

  std::string leading_whitespace_key;
  EXPECT_FALSE(KeyDisplayNameToKey(" 4D", &leading_whitespace_key));

  std::string junk_characters_in_hex_portion_key;
  EXPECT_FALSE(KeyDisplayNameToKey("(\" Junk characters! \") 3#D25ABC402>C71.8UF!28",
                                   &junk_characters_in_hex_portion_key));

  std::string too_long_parenthesized_key;
  EXPECT_FALSE(KeyDisplayNameToKey(
      "(\"" + std::string(kMaxKeySize + 1, '3') + "\") " + std::string(kMaxKeySize * 2 + 2, '3'),
      &too_long_parenthesized_key));

  std::string too_long_unparenthesized_key;
  EXPECT_FALSE(
      KeyDisplayNameToKey(std::string(kMaxKeySize * 2 + 2, '4'), &too_long_unparenthesized_key));
}

}  // namespace
}  // namespace ledger

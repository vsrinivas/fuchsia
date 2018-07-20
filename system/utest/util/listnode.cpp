// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/listnode.h>
#include <unittest/unittest.h>

namespace {

typedef struct list_elem {
    int value;
    list_node_t node;
} list_elem_t;

void expect_list_sorted(list_node_t* list, int count) {
    EXPECT_EQ(list_length(list), static_cast<unsigned int>(count));
    int index = 0;
    list_elem_t* entry = NULL;
    list_for_every_entry(list, entry, list_elem_t, node) {
        EXPECT_EQ(entry->value, index);
        EXPECT_EQ(list_next_wrap_type(list, &entry->node, list_elem_t, node)->value,
                  (index + 1) % count);
        EXPECT_EQ(list_prev_wrap_type(list, &entry->node, list_elem_t, node)->value,
                  (index + count - 1) % count);
        index++;
    }
}

bool initialize_empty_list() {
    BEGIN_TEST;

    list_node_t list = LIST_INITIAL_CLEARED_VALUE;
    EXPECT_FALSE(list_in_list(&list));

    list_initialize(&list);
    EXPECT_TRUE(list_in_list(&list));
    EXPECT_TRUE(list_is_empty(&list));
    EXPECT_EQ(list_length(&list), 0u);

    EXPECT_NULL(list_peek_head(&list));
    EXPECT_NULL(list_peek_head_type(&list, list_elem_t, node));
    EXPECT_NULL(list_peek_tail(&list));
    EXPECT_NULL(list_peek_tail_type(&list, list_elem_t, node));

    EXPECT_NULL(list_remove_head(&list));
    EXPECT_NULL(list_remove_head_type(&list, list_elem_t, node));
    EXPECT_NULL(list_remove_tail(&list));
    EXPECT_NULL(list_remove_tail_type(&list, list_elem_t, node));

    EXPECT_NULL(list_next(&list, &list));
    EXPECT_NULL(list_next_type(&list, &list, list_elem_t, node));
    EXPECT_NULL(list_next_wrap(&list, &list));
    EXPECT_NULL(list_next_wrap_type(&list, &list, list_elem_t, node));
    EXPECT_NULL(list_prev(&list, &list));
    EXPECT_NULL(list_prev_type(&list, &list, list_elem_t, node));
    EXPECT_NULL(list_prev_wrap(&list, &list));
    EXPECT_NULL(list_prev_wrap_type(&list, &list, list_elem_t, node));

    END_TEST;
}

bool element_add_remove() {
    BEGIN_TEST;

    list_elem_t first_set[5] = {
        { -1, {} },
        { 2, {} },
        { 3, {} },
        { 4, {} },
        { -1, {} },
    };
    list_elem_t second_set[4] = {
        { 0, {} },
        { 6, {} },
        { 1, {} },
        { 5, {} },
    };

    // Fill a list with elements from first_set.  [ -1 2 3 4 -1 ]
    list_node_t list = LIST_INITIAL_CLEARED_VALUE;
    list_initialize(&list);
    for (int i = 0; i < 5; i++) {
        list_add_tail(&list, &first_set[i].node);
    }

    // Clear the first and last elements in the list, then add new elements in
    // numerical order.  [ 0 1 2 3 4 5 ]
    list_remove_head(&list);
    list_remove_tail(&list);
    list_add_head(&list, &second_set[0].node);
    list_add_tail(&list, &second_set[1].node);
    list_add_after(list_peek_head(&list), &second_set[2].node);
    list_add_before(list_peek_tail(&list), &second_set[3].node);

    // The list should be sorted now.
    expect_list_sorted(&list, 7);

    // Verify list deletion.
    list_node_t* node = NULL;
    list_node_t* to_del = NULL;
    list_for_every_safe(&list, node, to_del) {
        list_delete(node);
    }
    EXPECT_TRUE(list_is_empty(&list));

    END_TEST;
}

bool list_splice_split() {
    BEGIN_TEST;

    list_elem_t first_set[3] = {
        { 0, {} },
        { 3, {} },
        { 4, {} },
    };
    list_elem_t second_set[3] = {
        { 5, {} },
        { 1, {} },
        { 2, {} },
    };

    list_node_t first_list;
    list_initialize(&first_list);
    list_node_t second_list;
    list_initialize(&second_list);

    for (int i = 0; i < 3; ++i) {
      list_add_tail(&first_list, &first_set[i].node);
      list_add_tail(&second_list, &second_set[i].node);
    }

    // Splice together the initial big list.  [ 0 3 4 5 1 2 ]
    list_splice_after(&second_list, list_peek_tail(&first_list));
    EXPECT_EQ(list_length(&first_list), 6u);
    EXPECT_EQ(list_length(&second_list), 0u);

    // Split off the last two elements of the list.  [ 0 3 4 5 ] [ 1 2 ]
    list_split_after(&first_list, list_peek_tail(&first_list)->prev->prev,
                     &second_list);
    EXPECT_EQ(list_length(&first_list), 4u);
    EXPECT_EQ(list_length(&second_list), 2u);

    // Splice the split portion back in, in order.  [ 0 1 2 3 4 5 ]
    list_splice_after(&second_list, list_peek_head(&first_list));
    EXPECT_EQ(list_length(&first_list), 6u);
    EXPECT_EQ(list_length(&second_list), 0u);

    // The list should be sorted now.
    expect_list_sorted(&first_list, 6);

    // Move the lists and recheck.
    list_move(&first_list, &second_list);
    EXPECT_EQ(list_length(&first_list), 0u);
    EXPECT_EQ(list_length(&second_list), 6u);

    // The second list should be sorted now.
    expect_list_sorted(&second_list, 6);

    END_TEST;
}

}  // namespace

BEGIN_TEST_CASE(listnode_tests)
RUN_TEST(initialize_empty_list);
RUN_TEST(element_add_remove);
RUN_TEST(list_splice_split);
END_TEST_CASE(listnode_tests)

#ifndef BUILD_COMBINED_TESTS
int main(int argc, char** argv) {
    bool success = unittest_run_all_tests(argc, argv);
    return success ? 0 : -1;
}
#endif  // BUILD_COMBINED_TESTS

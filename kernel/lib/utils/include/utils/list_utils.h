// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

namespace utils {

template <typename List, typename UnaryFn>
void for_each(List* list, UnaryFn fn) {
    for (typename List::ValueType* node = list->first(); node; node = list->next(node)) {
        fn(node);
    }
}

template <typename List, typename UnaryFn>
typename List::ValueType* find_if(List* list, UnaryFn fn) {
    for (typename List::ValueType* node = list->first(); node; node = list->next(node)) {
        if (fn(node)) return node;
    }
    return nullptr;
}

template <typename List, typename UnaryFn>
typename List::ValueType* pop_if(List* list, UnaryFn fn) {
    typename List::ValueType* prev = nullptr;
    for (typename List::ValueType* node = list->first(); node; node = list->next(node)) {
        if (fn(node)) return prev ? list->pop_next(prev) : list->pop_front();
        prev = node;
    }
    return nullptr;
}

template <typename List, typename UnaryFn>
size_t move_if(List* list_src, List* list_dst, UnaryFn fn) {
    size_t count = 0;
    typename List::ValueType* prev = nullptr;
    typename List::ValueType* node = list_src->first();

    while (node) {
        if (!fn(node)) {
            prev = node;
            node = list_src->next(node);
            continue;
        }

        typename List::ValueType* moved;
        if (prev) {
            moved = list_src->pop_next(prev);
            node = prev;
        } else {
            moved = list_src->pop_front();
            node = list_src->first();
        }

        list_dst->push_front(moved);
        ++count;
    }
    return count;
}

}  // namespace utils

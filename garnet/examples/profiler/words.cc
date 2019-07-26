/*
  Adapted from:
  https://github.com/akalenuk/wordsandbuttons/blob/master/exp/sort/radix/tests.cpp

  This is free and unencumbered software released into the public domain.

  Anyone is free to copy, modify, publish, use, compile, sell, or
  distribute this software, either in source code form or as a compiled
  binary, for any purpose, commercial or non-commercial, and by any
  means.

  In jurisdictions that recognize copyright laws, the author or authors
  of this software dedicate any and all copyright interest in the
  software to the public domain. We make this dedication for the benefit
  of the public at large and to the detriment of our heirs and
  successors. We intend this dedication to be an overt act of
  relinquishment in perpetuity of all present and future rights to this
  software under copyright law.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
  EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
  MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
  IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR
  OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
  ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
  OTHER DEALINGS IN THE SOFTWARE.

  For more information, please refer to <http://unlicense.org>
*/

#include "trie.h"

#include <algorithm>
#include <array>
#include <iostream>
#include <string>
#include <vector>
//#include <cassert>
#include <zircon/assert.h>
#include <chrono>
#include <fstream>
#include <map>
#include <random>
#include <unordered_map>

using namespace std::chrono;
using namespace std;

constexpr unsigned int sort_words = 1'000'000;
constexpr unsigned int sort_smallest = 3;
constexpr unsigned int sort_largest = 4;

constexpr unsigned int map_words = 100'000;
constexpr unsigned int map_smallest = 2;
constexpr unsigned int map_largest = 8;

vector<string> made_up_words(unsigned int how_much, unsigned int smallest, unsigned int largest) {
  vector<string> words;
  std::mt19937 rng(0);
  std::uniform_int_distribution<unsigned int> word_sizes(smallest, largest);
  std::uniform_int_distribution<char> word_letter('a', 'z');
  for (auto i = 0u; i < how_much; ++i) {
    auto word_size = word_sizes(rng);
    string word;
    for (auto j = 0u; j < word_size; ++j)
      word.push_back(word_letter(rng));
    words.push_back(word);
  }
  return words;
}

void functional_tests() {
  vector<string> unsorted = {"cat", "pat", "bed", "test", "test but longer", "test"};
  vector<string> std_sorted(unsorted.begin(), unsorted.end());
  sort(std_sorted.begin(), std_sorted.end());
  Trie::Set<4> trie_set;
  for (const auto& s : unsorted)
    trie_set.store(s);

  // sort test
  vector<string> sorted;
  Trie::Set<4>::fill_vector_sorted(&trie_set, sorted);
  for (auto i = 0u; i < unsorted.size(); ++i)
    ZX_ASSERT(sorted[i] == std_sorted[i]);

  // primitive tests for set
  for (const auto& s : unsorted)
    ZX_ASSERT(trie_set.contains(s));

  ZX_ASSERT(!trie_set.contains("not"));

  // primitive tests for map
  Trie::Map<string, 4> trie_map;
  for (const auto& s : unsorted)
    trie_map.store(s, s);

  for (const auto& s : unsorted)
    ZX_ASSERT(s == trie_map.retrieve(s).second);

  ZX_ASSERT(!trie_map.retrieve("not").first);
  ZX_ASSERT(trie_map.retrieve("not").second == "");
}

// prints out timings for different radix sizes
template <unsigned int RADIX_BITS>
void radix_sort_performance_print(const vector<string>& words) {
  auto radix_start = high_resolution_clock::now();
  Trie::Set<RADIX_BITS> trie;
  for (const auto& word : words)
    trie.store(word);
  vector<string> sorted_words;
  sorted_words.reserve(words.size());
  Trie::Set<RADIX_BITS>::fill_vector_sorted(&trie, sorted_words);
  auto radix_sort_duration =
      duration_cast<milliseconds>(high_resolution_clock::now() - radix_start).count();
  cout << "   radix " << RADIX_BITS << " sort - " << radix_sort_duration << "\n";
}

void sort_performance_prints(vector<string>& words) {
  cout << "Sorting performance\n";

  // std::sort
  auto std_start = high_resolution_clock::now();
  vector<string> std_sorted_words(words.begin(), words.end());
  sort(std_sorted_words.begin(), std_sorted_words.end());
  auto std_duration = duration_cast<milliseconds>(high_resolution_clock::now() - std_start).count();
  cout << "   std::sort - " << std_duration << "\n";

  // radix sort
  radix_sort_performance_print<1>(words);
  radix_sort_performance_print<2>(words);
  radix_sort_performance_print<4>(words);
  radix_sort_performance_print<8>(words);

  cout << "\n";
}

// prints out timings and memory intakes for different radix sizes
template <unsigned int RADIX_BITS>
void radix_map_performance_print(vector<string>& dic) {
  auto dic_size = dic.size();
  cout << "Trie::Map with " << RADIX_BITS << "-bits radix\n";
  Trie::Map<string*, RADIX_BITS> test_trie;
  auto start = high_resolution_clock::now();
  for (size_t j = 0; j < 100; j++) {
    for (size_t i = 0; i < dic_size; i++) {
      test_trie.store(dic[i], &dic[i]);
    }
  }
  auto duration = duration_cast<milliseconds>(high_resolution_clock::now() - start).count();
  cout << "   Writing: " << duration << "\n";

  start = high_resolution_clock::now();
  for (size_t j = 0; j < 100; j++) {
    for (size_t i = 0; i < dic_size; i++) {
      string* back = test_trie.retrieve(dic[i]).second;
      if (back != &dic[i]) {
        cout << "error with " << dic[i] << "\n";
      }
    }
  }
  duration = duration_cast<milliseconds>(high_resolution_clock::now() - start).count();
  cout << "   Reading: " << duration << "\n";
  cout << "   Size in bytes: " << test_trie.size_in_bytes() << "\n\n";
}

void map_performance_prints(vector<string>& dic) {
  auto dic_size = dic.size();

  // trie as a map
  radix_map_performance_print<1>(dic);
  radix_map_performance_print<2>(dic);
  radix_map_performance_print<4>(dic);
  radix_map_performance_print<8>(dic);

  cout << "std::map\n";

  // map (as binary tree representative)
  map<string, string*> test_map;

  auto start = high_resolution_clock::now();
  for (size_t j = 0; j < 100; j++) {
    for (size_t i = 0; i < dic_size; i++) {
      test_map[dic[i]] = &dic[i];
    }
  }
  auto duration = duration_cast<milliseconds>(high_resolution_clock::now() - start).count();
  cout << "   Writing: " << duration << "\n";

  start = high_resolution_clock::now();
  for (size_t j = 0; j < 100; j++) {
    for (size_t i = 0; i < dic_size; i++) {
      string* back = test_map[dic[i]];
      if (back != &dic[i]) {
        cout << "error with " << dic[i] << "\n";
      }
    }
  }
  duration = duration_cast<milliseconds>(high_resolution_clock::now() - start).count();
  cout << "   Reading: " << duration << "\n\n";

  // unordered map as hash-table representative
  cout << "std::unordered_map\n";

  unordered_map<string, string*> test_unordered_map;

  start = high_resolution_clock::now();
  for (size_t j = 0; j < 100; j++) {
    for (size_t i = 0; i < dic_size; i++) {
      test_unordered_map[dic[i]] = &dic[i];
    }
  }
  duration = duration_cast<milliseconds>(high_resolution_clock::now() - start).count();
  cout << "   Writing: " << duration << "\n";

  start = high_resolution_clock::now();
  for (size_t j = 0; j < 100; j++) {
    for (size_t i = 0; i < dic_size; i++) {
      string* back = test_unordered_map[dic[i]];
      if (back != &dic[i]) {
        cout << "error with " << dic[i] << "\n";
      }
    }
  }
  duration = duration_cast<milliseconds>(high_resolution_clock::now() - start).count();
  cout << "   Reading: " << duration << "\n\n";
}

void words_tests() {
  functional_tests();

  auto words_to_sort = made_up_words(sort_words, sort_smallest, sort_largest);
  sort_performance_prints(words_to_sort);

  auto words_to_store = made_up_words(map_words, map_smallest, map_largest);
  sort(words_to_store.begin(), words_to_store.end());
  words_to_store.erase(unique(words_to_store.begin(), words_to_store.end()), words_to_store.end());
  map_performance_prints(words_to_store);
}

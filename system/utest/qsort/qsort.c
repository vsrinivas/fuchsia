// Copyright 2016 The Fuchsia Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>

typedef int (*comparator)(const void*, const void*);

#include <stdio.h>

static void verify_sorted(char* array, size_t length, size_t element_size, comparator cmp) {
    assert(length > 0);

    for (size_t idx = 1; idx < length; idx++) {
        char* left = array + (idx - 1) * element_size;
        char* right = array + idx * element_size;
        int c = cmp(left, right);
        if (c > 0) {
            printf("%zu %zu\n", idx - 1, idx);
            abort();
        }
    }
}

static int uint32_t_cmp(const void* void_left, const void* void_right) {
    uint32_t left = *(const uint32_t*)void_left;
    uint32_t right = *(const uint32_t*)void_right;
    if (left < right)
        return -23;
    else if (left > right)
        return 42;
    else
        return 0;
}

static uint32_t test_data[] = {
    0xfd93ad4b, 0xfbc93cfc, 0x2042953f, 0x45f58b5d, 0x139aded5, 0xf07894da, 0xfc6f3f63, 0xd266dac2,
    0x6a09d2c9, 0x472963a6, 0x72ff8380, 0x77aef16e, 0x97d3b43e, 0x1ec37308, 0x8afcc082, 0xaf555791,
    0x27c148b3, 0x4cc07328, 0x1437ddcb, 0x9143a405, 0xd25f9d59, 0x4b013ae3, 0x09c9926e, 0x16323444,
    0xcf772eaa, 0x30fb2777, 0xeb805772, 0xa0996cd2, 0xa59f4e3b, 0x7d6337ed, 0x7163e2bc, 0x83d840b7,
    0x0cd31ff0, 0x96df9945, 0x30f77172, 0x876f460d, 0x2a9ae4a4, 0x3a7fe7d2, 0xdc6b40b5, 0xd4346410,
    0x2a876b4a, 0x265a2fa0, 0x107afc8b, 0xde242755, 0x0ae34c30, 0x497c2f15, 0x4b489e3b, 0x9fc7f96e,
    0xbe20c9c4, 0x27d1a3b5, 0x26317d23, 0x2720afa9, 0x8ebd9f38, 0x7d8277be, 0x85aca8e8, 0x71cb98d0,
    0xc0d1b711, 0x831868e3, 0x05bc3fb0, 0x0a22c7ed, 0x27291a38, 0xbf04e1a7, 0x7707d917, 0x3abcab71,
    0xd6ba4a0e, 0x65aa7f35, 0xdee2bfd9, 0x52497e8c, 0x4b6ef8e5, 0x73c4bdad, 0x222febe6, 0x5560f245,
    0x9b40283f, 0x605a2f7f, 0xb2a0aa94, 0xca366ff9, 0xe0407032, 0x6eb04055, 0xa0d6dfbd, 0xbe20d149,
    0xe28d07b8, 0xcb7cfee9, 0x4c64ee95, 0xda9f948c, 0x20bea02d, 0x58635300, 0x49ede10b, 0xdf2acc68,
    0xeb965466, 0xe5f40b17, 0x9102a5a1, 0x92e1dde4, 0x73c28587, 0xd1e0ca1a, 0xb5df028b, 0xff253fe2,
    0x723a1b88, 0x5c792fc9, 0x7a3f645a, 0x317fab1a, 0x940f4a4d, 0xbd0545d8, 0x1d349c89, 0xf3746429,
    0xd9a8a7bf, 0xaf24eed2, 0xdb48f4e8, 0xe2d1e6e1, 0xa60317f9, 0x490bf590, 0xd7aabb79, 0x32b2e7d8,
    0xd6fd3d51, 0x822a69ac, 0xf79a3deb, 0x1093a5b7, 0x96a9b83f, 0xc3345f2b, 0xf1fda702, 0xfd895346,
    0x56c2ae32, 0xdd9e52d3, 0x86f7cf57, 0x58c320ca, 0xdfc296d4, 0x6681d69e, 0x43a27d19, 0xd37988f5,
};

int main(void) {
    qsort(test_data, sizeof(test_data) / sizeof(*test_data), sizeof(*test_data), uint32_t_cmp);
    verify_sorted((char*)test_data, sizeof(test_data) / sizeof(*test_data), sizeof(*test_data), uint32_t_cmp);
}

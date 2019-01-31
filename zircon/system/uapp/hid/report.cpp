// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>

#include <hid-parser/item.h>

namespace {

const char* TypeName(hid::Item::Type t) {
    switch (t) {
#define TYPE(t) case hid::Item::Type::k##t: return #t
    TYPE(Main);
    TYPE(Global);
    TYPE(Local);
    TYPE(Reserved);
#undef TYPE
    default: return "(unknown)";
    }
}

const char* TagName(hid::Item::Tag t) {
    switch (t) {
#define TAG(t) case hid::Item::Tag::k##t: return #t
    TAG(Input);
    TAG(Output);
    TAG(Feature);
    TAG(Collection);
    TAG(EndCollection);
    TAG(UsagePage);
    TAG(LogicalMinimum);
    TAG(LogicalMaximum);
    TAG(PhysicalMinimum);
    TAG(PhysicalMaximum);
    TAG(UnitExponent);
    TAG(Unit);
    TAG(ReportSize);
    TAG(ReportId);
    TAG(ReportCount);
    TAG(Push);
    TAG(Pop);
    TAG(Usage);
    TAG(UsageMinimum);
    TAG(UsageMaximum);
    TAG(DesignatorIndex);
    TAG(DesignatorMinimum);
    TAG(DesignatorMaximum);
    TAG(StringIndex);
    TAG(StringMinimum);
    TAG(StringMaximum);
    TAG(Delimiter);
    TAG(Reserved);
#undef TAG
    default: return "(unknown)";
    }
}
}  // namespace

void print_report_descriptor(const uint8_t* rpt_desc, size_t desc_len) {
    const uint8_t* buf = rpt_desc;
    size_t len = desc_len;
    int indent = 0;
    const int kIndentChars = 4;

    while (len > 0) {
        size_t item_actual = 0;
        auto item = hid::Item::ReadNext(buf, len, &item_actual);

        if (item_actual > len) {
            printf("%zu bytes needed for item\n", item_actual);
            break;
        }

        if (item_actual == 0) {
            printf("Error parsing report stream.\n");
            break;
        }

        if (item.tag() == hid::Item::Tag::kEndCollection) {
            if (indent < kIndentChars) {
                printf("unmatched ==> ");
            } else {
                indent -= kIndentChars;
            }
        }

        printf("%*s Item(%s, %s): %0#x\n", indent, "",
            TypeName(item.type()), TagName(item.tag()), item.data());

        if (item.tag() == hid::Item::Tag::kCollection) {
            indent += kIndentChars;
        }

        len -= item_actual;
        buf += item_actual;
    }

    if (len > 0) {
        printf("%zu bytes not consumed.\n", len);
    }
}

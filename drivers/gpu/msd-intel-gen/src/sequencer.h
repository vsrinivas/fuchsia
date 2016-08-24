// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SEQUENCER_H
#define SEQUENCER_H

#include "magma_util/macros.h"
#include "msd_intel_buffer.h"

class Sequencer {
public:
    Sequencer(uint32_t first_sequence_number) : next_sequence_number_(first_sequence_number) {}

    uint32_t next_sequence_number()
    {
        uint32_t sequence_number = next_sequence_number_++;
        // Overflow not handled yet.
        DASSERT(next_sequence_number_ >= sequence_number);
        return sequence_number;
    }

private:
    uint32_t next_sequence_number_;
};

#endif // SEQUENCER_H

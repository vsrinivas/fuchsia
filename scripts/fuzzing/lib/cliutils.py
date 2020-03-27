#!/usr/bin/env python2.7
# Copyright 2019 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.


def show_menu(choices):
    """Displays a simple interactive menu, looping until a valid selection is made."""

    for i, choice in enumerate(choices, 1):
        print("  {}) {}".format(i, choice))

    prompt = "Choose 1-{}: ".format(len(choices))

    choice = None
    while not choice:
        try:
            choice = choices[int(raw_input(prompt)) - 1]
        except ValueError, IndexError:
            print("Invalid selection.")

    return choice

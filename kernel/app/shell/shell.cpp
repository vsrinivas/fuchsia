// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2009 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <app.h>
#include <debug.h>
#include <lib/console.h>

static void shell_init(const struct app_descriptor* app) {
    console_init();
}

static void shell_entry(const struct app_descriptor* app, void* args) {
    console_start();
}

APP(shell, shell_init, shell_entry);

// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/boot-options/boot-options.h>

#include <lk/init.h>
#include <vm/physical_page_borrowing_config.h>
#include <vm/pmm.h>

static void ppb_init_func(uint level) {
  // One option per potential borrowing site.
  pmm_physical_page_borrowing_config()->set_borrowing_in_supplypages_enabled(
      gBootOptions->ppb_borrow_in_supplypages);

  pmm_physical_page_borrowing_config()->set_borrowing_on_mru_enabled(
      gBootOptions->ppb_borrow_on_mru);

  // One option for whether decommit on contiguous VMO can work or returns ZX_ERR_NOT_SUPPORTED.
  pmm_physical_page_borrowing_config()->set_loaning_enabled(gBootOptions->ppb_loan);
}

LK_INIT_HOOK(ppb_init, &ppb_init_func, LK_INIT_LEVEL_VM)

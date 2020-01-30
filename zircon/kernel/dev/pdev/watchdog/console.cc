// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#if LK_DEBUGLEVEL > 1

#include <lib/console.h>
#include <lib/debuglog.h>
#include <platform.h>

#include <dev/watchdog.h>

static void usage(const char* cmd_name) {
  printf("Usage:\n");
  printf("%s status  : show the recent status of the hardware watchdog subsystem.\n", cmd_name);
  printf("%s pet     : force an immediate pet of the watchdog.\n", cmd_name);
  printf("%s enable  : attempt to enable the watchdog.\n", cmd_name);
  printf("%s disable : attempt to disable the watchdog.\n", cmd_name);
  printf("%s force   : force the watchdog to fire.\n", cmd_name);
  printf("%s help    : show this message.\n", cmd_name);
}

enum class Cmd {
  Status,
  Pet,
  Enable,
  Disable,
  Force,
};

static int cmd_watchdog(int argc, const cmd_args* argv, uint32_t flags) {
  if (argc < 2) {
    printf("Not enough arguments.\n");
    usage(argv[0].str);
    return -1;
  }

  Cmd command;
  if (!strcmp(argv[1].str, "status")) {
    command = Cmd::Status;
  } else if (!strcmp(argv[1].str, "pet")) {
    command = Cmd::Pet;
  } else if (!strcmp(argv[1].str, "enable")) {
    command = Cmd::Enable;
  } else if (!strcmp(argv[1].str, "disable")) {
    command = Cmd::Disable;
  } else if (!strcmp(argv[1].str, "force")) {
    command = Cmd::Force;
  } else if (!strcmp(argv[1].str, "help")) {
    usage(argv[0].str);
    return 0;
  } else {
    printf("Unrecognized command.\n");
    usage(argv[0].str);
    return -1;
  }

  if (!watchdog_present()) {
    printf("There is no hardware watchdog present in this system.\n");
    return 0;
  }

  switch (command) {
    case Cmd::Status: {
      zx_time_t last_pet = watchdog_get_last_pet_time();
      zx_time_t now = current_time();
      printf("Enabled  : %s\n", watchdog_is_enabled() ? "yes" : "no");
      printf("Timeout  : %ld nSec\n", watchdog_get_timeout_nsec());
      printf("Last Pet : %ld (%ld nSec ago)\n", last_pet, now - last_pet);
    } break;

    case Cmd::Pet: {
      printf("Watchdog has been pet.  She's a good girl! (yes she is!!)\n");
    } break;

    case Cmd::Enable: {
      zx_status_t res;
      res = watchdog_set_enabled(true);
      if (res == ZX_ERR_NOT_SUPPORTED) {
        printf("Watchdog does not support enabling.\n");
        return res;
      } else if (res != ZX_OK) {
        printf("Error enabling watchdog (%d)\n", res);
        return res;
      } else {
        printf("Watchdog enabled.\n");
      }
    } break;

    case Cmd::Disable: {
      zx_status_t res;
      res = watchdog_set_enabled(false);
      if (res == ZX_ERR_NOT_SUPPORTED) {
        printf("Watchdog does not support disabling.\n");
        return res;
      } else if (res != ZX_OK) {
        printf("Error disabling watchdog (%d)\n", res);
        return res;
      } else {
        printf("Watchdog disabled.\n");
      }
    } break;

    case Cmd::Force: {
      if (!watchdog_is_enabled()) {
        printf("Watchdog is not enabled.  Enable the watchdog first.\n");
        return ZX_ERR_BAD_STATE;
      }

      // In order to _really_ wedge the system we...
      // 1) Disable preemption for our thread.
      // 2) Migrate our thread to the boot core.
      // 3) Halt all of the secondary cores
      // 4) Disable interrupts.
      // 5) Spin forever.
      //
      thread_preempt_disable();
      thread_migrate_to_cpu(BOOT_CPU_ID);
      platform_halt_secondary_cpus();
      arch_disable_ints();

      // Make sure that our printf goes directly to the UART, bypassing any
      // buffering which is not going to get drained now that we have stopped
      // the system.
      dlog_force_panic();

      zx_time_t deadline = watchdog_get_last_pet_time() + watchdog_get_timeout_nsec();
      printf("System wedged!  Watchdog will fire in %ld nSec\n", deadline - current_time());

      // Spin forever.  The watchdog should reboot us.
      while (true)
        ;
    } break;
  };

  return 0;
}

STATIC_COMMAND_START
STATIC_COMMAND("watchdog", "watchdog commands", &cmd_watchdog)
STATIC_COMMAND_END(gfx)

#endif

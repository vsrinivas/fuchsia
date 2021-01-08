# Time language support

This page details how you can handle time when developing software on Fuchsia.

In general, you may use any time library available for your language. Most
libraries are able to link against [Fuchsia's libc implementation][libc] to
obtain time. It is recommended that you use a platform-agnostic library unless
you need Fuchsia specific time operations.

Various time options are available, such as:

Language | Monotonic | UTC
-------- | --------- | ---
C | [`clock_gettime`][c-clock-gettime]{:.external} | [`time`][c-time]{:.external}
C++ | [`std::chrono::steady_clock`][cpp-steady-clock]{:.external} | [`std::chrono::system_clock`][cpp-system-clock]{:.external}
Rust | [`std::time::Instant`][rust-instant]{:.external} | [`std::time::SystemTime`][rust-system-time]{:.external}

## Fuchsia specific time operations

In some cases, you will need to handle time in a Fuchsia specific manner. This
is necessary when you need to handle Fuchsia's representation of time directly,
or when you need to handle
[Fuchsia specific UTC behavior][fuchsia-utc-behavior].

### Monotonic time

Monotonic time on Fuchsia is represented as a signed 64 bit integer, which
contains the number of nanoseconds since the system was powered on. See
[`zx_clock_get_monotonic`][zx-monotonic] for more details.

* {C }

  Monotonic time is accessible through [libzircon][c-libzircon].

  ```c
  #include <stdio.h>
  #include <zircon/syscalls.h>

  {
    zx_time_t mono_nsec = zx_clock_get_monotonic();
    printf("The monotonic time is %ld ns.\n", mono_nsec);
  }
  ```

* {C++}

  Monotonic time is accessible through [libzx][cpp-libzx].

  ```cpp
    #include <lib/zx/clock.h>
    #include <lib/zx/time.h>
    #include <stdio.h>

    {
      zx::time monotonic_time = zx::clock::get_monotonic();
      printf("The monotonic time is %ld ns.\n", monotonic_time.get());
    }
  ```

* {Rust}

  Monotonic time is accessible through the [fuchsia_zircon][rust-zircon] crate.
  This crate is only available in-tree.

  ```rust
    use fuchsia_zircon as zx;

    {
      let monotonic_time = zx::Time::get_monotonic();
      println!("The monotonic time is {:?} ns.", monotonic_time);
    }
  ```

### UTC time

UTC time on Fuchsia is represented as a signed 64 bit integer which contains
the number of nanoseconds since the Unix epoch (January 1st, 1970).

Operations on the UTC clock require obtaining a handle to the UTC clock
provided to the runtime.

Handling the UTC clock directly enables a few Fuchsia specific operations,
including:

* Inspecting the UTC clock's properties.
* Waiting for the UTC clock to begin running, which indicates it has been
synchronized, before reading it.

* {C }

  You can obtain a handle to the UTC clock using `zx_utc_reference_get` and use
  the syscalls exposed in [libzircon][c-libzircon].

  ```c
  #include <zircon/utc.h>
  #include <zircon/syscalls.h>
  #include <zircon/syscalls/clock.h>
  #include <stdio.h>

  int main(int argc, const char** argv) {
    // This is a borrowed handle. Do not close it, and do not replace it using
    // zx_utc_reference_swap while using it.
    zx_handle_t utc_clock = zx_utc_reference_get();

    if (utc_clock != ZX_HANDLE_INVALID) {
      // Wait for the UTC clock to start.
      zx_status_t status =
        zx_object_wait_one(utc_clock, ZX_CLOCK_STARTED, ZX_TIME_INFINITE, NULL);
      if (status == ZX_OK) {
        printf("UTC clock is started.\n");
      } else {
        printf("zx_object_wait_one syscall failed (status = %d).\n", status);
      }

      // Read the UTC clock.
      zx_time_t nsec;
      status = zx_clock_read(utc_clock, &nsec);
      if (status == ZX_OK) {
        printf("It has been %ld nSec since the epoch.\n", nsec);
      } else {
        printf("zx_clock_read syscall failed (status = %d).\n", status);
      }

      // Read UTC clock details.
      zx_clock_details_v1_t details;
      status = zx_clock_get_details(utc_clock, ZX_CLOCK_ARGS_VERSION(1), &details);
      if (status == ZX_OK) {
        printf("The UTC clock's backstop time is %ld ns since the epoch.\n",
          details.backstop_time);
      } else {
        printf("zx_clock_get_details failed (status = %d).\n", status);
      }

    } else {
      printf("Error, our runtime has no clock assigned to it!\n");
    }
  }
  ```

* {C++}

  You can obtain a handle to the UTC clock using `zx_utc_reference_get` and use
  the syscall wrappers in [libzx][cpp-libzx].

  ```cpp
  #include <stdio.h>
  #include <lib/zx/clock.h>
  #include <lib/zx/time.h>
  #include <zircon/utc.h>

  int main(int argc, const char** argv) {
    // This is a borrowed handle. Do not close it, and do not replace it using
    // zx_utc_reference_swap while using it.
    zx_handle_t utc_clock_handle = zx_utc_reference_get();
    zx::unowned_clock utc_clock(utc_clock_handle);

    // Wait for the UTC clock to start.
    zx_status_t status =
      utc_clock->wait_one(ZX_CLOCK_STARTED, zx::time::infinite(), NULL);
    if (status == ZX_OK) {
      printf("UTC clock is started.\n");
    } else {
      printf("Waiting for the UTC clock to start failed (status = %d).\n", status);
    }

    // Read the UTC clock.
    zx_time_t utc_time;
    status = utc_clock->read(&utc_time);
    if (status == ZX_OK) {
      printf("The UTC time is %ld ns since the epoch\n", utc_time);
    } else {
      printf("Reading the UTC clock failed (status = %d).\n", status);
    }

    // Read clock details.
    zx_clock_details_v1_t details;
    status = utc_clock->get_details(&details);
    if (status == ZX_OK) {
      printf("The UTC clock's backstop time is %ld ns since the epoch.\n",
        details.backstop_time);
    } else {
      printf("Reading the UTC clock details failed (status = %d).\n", status);
    }
  }
  ```

* {Rust}

  You can obtain a handle to the UTC clock using the
  [fuchsia_runtime][rust-runtime] crate and use the syscall wrappers in the
  [fuchsia_zircon][rust-zircon] crate. The [fuchsia_async][rust-async] crate
  contains utilities to aid waiting for the clock to start. Note that these
  crates are only available in-tree.

  ```rust
  use fuchsia_async as fasync;
  use fuchsia_runtime::duplicate_utc_clock_handle;
  use fuchsia_zircon as zx;

  #[fasync::run_singlethreaded]
  async fn main() {
      // Obtain a UTC handle.
      let utc_clock = duplicate_utc_clock_handle(zx::Rights::SAME_RIGHTS)
          .expect("Failed to duplicate UTC clock handle.");

      // Wait for the UTC clock to start.
      fasync::OnSignals::new(&utc_clock, zx::Signals::CLOCK_STARTED)
          .await
          .expect("Failed to wait for ZX_CLOCK_STARTED.");
      println!("UTC clock is started.");

      // Read the UTC clock.
      let utc_time = utc_clock.read().expect("Failed to read UTC clock.");
      println!("The UTC time is {:?} ns since the epoch.", utc_time);

      // Read UTC clock details.
      let clock_details = utc_clock.get_details().expect("Failed to read UTC clock details.");
      println!("The UTC clock's backstop time is {:?} ns since the epoch.",  
        clock_details.backstop);
  }
  ```

[libc]: /docs/development/languages/c-cpp/libc.md
[c-clock-gettime]: https://linux.die.net/man/3/clock_gettime
[c-time]: https://linux.die.net/man/2/time
[cpp-steady-clock]: https://en.cppreference.com/w/cpp/chrono/steady_clock
[cpp-system-clock]: https://en.cppreference.com/w/cpp/chrono/system_clock
[rust-instant]: https://doc.rust-lang.org/std/time/struct.Instant.html
[rust-system-time]: https://doc.rust-lang.org/std/time/struct.SystemTime.html
[fuchsia-utc-behavior]: utc/behavior.md#differences_from_other_operating_systems
[zx-monotonic]: /docs/reference/syscalls/clock_get_monotonic.md
[c-libzircon]: /docs/concepts/framework/core_libraries.md#libzircon
[cpp-libzx]: /docs/concepts/framework/core_libraries.md#libzx
[rust-runtime]: /src/lib/fuchsia-runtime
[rust-zircon]: /src/lib/zircon/rust
[rust-async]: /src/lib/fuchsia-async

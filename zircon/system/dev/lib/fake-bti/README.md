This library provides fake replacements for the BTI and PMT syscalls for the
purpose of testing driver code in an unprivileged environment.  It works by
defining strong symbols for the following system calls:

- **zx_object_get_info**()
- **zx_bti_pin**()
- **zx_bti_release_quarantine**().
- **zx_pmt_unpin**()

The library exposes methods for creating and destroying fake BTI "handles" that
are compatible with the fake BTI syscalls.  It is not safe to use any other
handle operations on the fake BTI and PMT handles.

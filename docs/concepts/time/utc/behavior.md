# UTC Behavior

TODO([65784](https://fxbug.dev/65784)): Populate this page to document the
behavior that users of the UTC clock can expect, including the following points
as a minimum:

* How to get a clock handle.
* Clock handle is a process variable, if the parent didn’t supply it it won’t be
  available.
* Under no circumstances will the clock ever be earlier than backstop.
* Until UTC is available the clock will remain fixed at the backstop time.
* ZX_CLOCK_STARTED may be waited on to learn when time is available.
* The clock may step either forward or backward and there is no upper limit on
  the size of this step.
* Steps should be rare on most devices, except for the initial step from
  backstop time to the accurate time when the clock is first synchronized.
* The clock may run at a different rate than monotonic to compensate for
  oscillator errors or to gradually remove small errors, should never exceed a
  few hundred ppm in either direction.
* How to get clock details.
* Definition of error bound in clock details (when we agree this).
* Accuracy will vary from product to product but should be accurate to within a
  few hundreds of milliseconds on most devices most of the time.
* What secure time means (when we agree this).
* How to tell when time is secure (when we agree this).

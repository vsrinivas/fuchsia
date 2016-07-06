# This is a header only library that everything else in userspace
# relies on. As such, we take only very minimal dependencies here:

#  - C headers for integer types (stdbool.h, stdint.h, stddef.h)
#  - Compiler builtins (mostly abstracted by our own compiler.h)

# An example cool thing to add here would be a header-only inline count-leading-zeroes.

# Anything that requires source or builds a library (even an empty
# one) is right out. Anything that needs to name more complicated
# types than the ones mentioned above is probably also out of scope
# here.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := userlib

MODULE_SRCS := $(LOCAL_DIR)/empty.c

MODULE_EXPORT := system

include make/module.mk

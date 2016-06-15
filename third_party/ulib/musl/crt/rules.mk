MODULE_SRCS += \
    $(GET_LOCAL_DIR)/crt1.c \

ifeq ($(ARCH),arm64)
MODULE_SRCS += \
    $(GET_LOCAL_DIR)/aarch64/crti.s \
    $(GET_LOCAL_DIR)/aarch64/crtn.s \

else ifeq ($(ARCH),arm)
MODULE_SRCS += \
    $(GET_LOCAL_DIR)/arm/crti.s \
    $(GET_LOCAL_DIR)/arm/crtn.s \

else ifeq ($(SUBARCH),x86-64)
MODULE_SRCS += \
    $(GET_LOCAL_DIR)/x86_64/crti.s \
    $(GET_LOCAL_DIR)/x86_64/crtn.s \

else
error Unsupported architecture for musl build!

endif

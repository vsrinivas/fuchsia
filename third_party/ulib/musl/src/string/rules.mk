LOCAL_SRCS += \
    $(GET_LOCAL_DIR)/bcmp.c \
    $(GET_LOCAL_DIR)/bcopy.c \
    $(GET_LOCAL_DIR)/bzero.c \
    $(GET_LOCAL_DIR)/index.c \
    $(GET_LOCAL_DIR)/memccpy.c \
    $(GET_LOCAL_DIR)/memmem.c \
    $(GET_LOCAL_DIR)/memrchr.c \
    $(GET_LOCAL_DIR)/rindex.c \
    $(GET_LOCAL_DIR)/stpcpy.c \
    $(GET_LOCAL_DIR)/stpncpy.c \
    $(GET_LOCAL_DIR)/strcasecmp.c \
    $(GET_LOCAL_DIR)/strcasestr.c \
    $(GET_LOCAL_DIR)/strcat.c \
    $(GET_LOCAL_DIR)/strcspn.c \
    $(GET_LOCAL_DIR)/strdup.c \
    $(GET_LOCAL_DIR)/strerror_r.c \
    $(GET_LOCAL_DIR)/strlcat.c \
    $(GET_LOCAL_DIR)/strlcpy.c \
    $(GET_LOCAL_DIR)/strncasecmp.c \
    $(GET_LOCAL_DIR)/strncat.c \
    $(GET_LOCAL_DIR)/strncpy.c \
    $(GET_LOCAL_DIR)/strndup.c \
    $(GET_LOCAL_DIR)/strpbrk.c \
    $(GET_LOCAL_DIR)/strrchr.c \
    $(GET_LOCAL_DIR)/strsep.c \
    $(GET_LOCAL_DIR)/strsignal.c \
    $(GET_LOCAL_DIR)/strspn.c \
    $(GET_LOCAL_DIR)/strstr.c \
    $(GET_LOCAL_DIR)/strtok.c \
    $(GET_LOCAL_DIR)/strtok_r.c \
    $(GET_LOCAL_DIR)/strverscmp.c \
    $(GET_LOCAL_DIR)/swab.c \
    $(GET_LOCAL_DIR)/wcpcpy.c \
    $(GET_LOCAL_DIR)/wcpncpy.c \
    $(GET_LOCAL_DIR)/wcscasecmp.c \
    $(GET_LOCAL_DIR)/wcscat.c \
    $(GET_LOCAL_DIR)/wcschr.c \
    $(GET_LOCAL_DIR)/wcscmp.c \
    $(GET_LOCAL_DIR)/wcscpy.c \
    $(GET_LOCAL_DIR)/wcscspn.c \
    $(GET_LOCAL_DIR)/wcsdup.c \
    $(GET_LOCAL_DIR)/wcslen.c \
    $(GET_LOCAL_DIR)/wcsncasecmp.c \
    $(GET_LOCAL_DIR)/wcsncat.c \
    $(GET_LOCAL_DIR)/wcsncmp.c \
    $(GET_LOCAL_DIR)/wcsncpy.c \
    $(GET_LOCAL_DIR)/wcsnlen.c \
    $(GET_LOCAL_DIR)/wcspbrk.c \
    $(GET_LOCAL_DIR)/wcsrchr.c \
    $(GET_LOCAL_DIR)/wcsspn.c \
    $(GET_LOCAL_DIR)/wcsstr.c \
    $(GET_LOCAL_DIR)/wcstok.c \
    $(GET_LOCAL_DIR)/wcswcs.c \
    $(GET_LOCAL_DIR)/wmemchr.c \
    $(GET_LOCAL_DIR)/wmemcmp.c \
    $(GET_LOCAL_DIR)/wmemcpy.c \
    $(GET_LOCAL_DIR)/wmemmove.c \
    $(GET_LOCAL_DIR)/wmemset.c \

ifeq ($(ARCH),arm64)

# These files do '#include "third_party/lib/cortex-strings/src/aarch64/..."'
LOCAL_COMPILEFLAGS += -I.

LOCAL_SRCS += \
    $(GET_LOCAL_DIR)/aarch64/memcpy.S \
    $(GET_LOCAL_DIR)/aarch64/memmove.S \
    $(GET_LOCAL_DIR)/aarch64/memset.S \

else ifeq ($(SUBARCH),x86-64)

LOCAL_SRCS += \
    $(GET_LOCAL_DIR)/x86_64/memcpy.S \
    $(GET_LOCAL_DIR)/x86_64/memmove.S \
    $(GET_LOCAL_DIR)/x86_64/memset.S \

else

LOCAL_SRCS += \
    $(GET_LOCAL_DIR)/memcpy.c \
    $(GET_LOCAL_DIR)/memmove.c \
    $(GET_LOCAL_DIR)/memset.c \

endif

# Only use the assembly version if x86-64 and not ASan.
ifeq ($(SUBARCH):$(call TOBOOL,$(USE_ASAN)),x86-64:false)
LOCAL_SRCS += $(GET_LOCAL_DIR)/x86_64/mempcpy.S
else
LOCAL_SRCS += $(GET_LOCAL_DIR)/mempcpy.c
endif

ifeq ($(ARCH),arm64)

LOCAL_SRCS += \
    third_party/lib/cortex-strings/src/aarch64/memchr.S \
    third_party/lib/cortex-strings/src/aarch64/memcmp.S \
    third_party/lib/cortex-strings/src/aarch64/strchr.S \
    $(GET_LOCAL_DIR)/aarch64/strchrnul.S \
    third_party/lib/cortex-strings/src/aarch64/strcmp.S \
    third_party/lib/cortex-strings/src/aarch64/strcpy.S \
    third_party/lib/cortex-strings/src/aarch64/strlen.S \
    third_party/lib/cortex-strings/src/aarch64/strncmp.S \
    third_party/lib/cortex-strings/src/aarch64/strnlen.S \

else

LOCAL_SRCS += \
    $(GET_LOCAL_DIR)/memchr.c \
    $(GET_LOCAL_DIR)/memcmp.c \
    $(GET_LOCAL_DIR)/strchr.c \
    $(GET_LOCAL_DIR)/strchrnul.c \
    $(GET_LOCAL_DIR)/strcmp.c \
    $(GET_LOCAL_DIR)/strcpy.c \
    $(GET_LOCAL_DIR)/strlen.c \
    $(GET_LOCAL_DIR)/strncmp.c \
    $(GET_LOCAL_DIR)/strnlen.c \

endif

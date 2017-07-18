# Compile Noise Source as user space application

CC=gcc
CFLAGS +=-Wextra -Wall -pedantic -fPIC -O0
#Hardening
CFLAGS +=-fstack-protector-strong -fwrapv --param ssp-buffer-size=4
LDFLAGS +=-Wl,-z,relro,-z,now

# Change as necessary
PREFIX := /usr/local
# library target directory (either lib or lib64)
LIBDIR := lib

NAME := jitterentropy
LIBMAJOR=$(shell cat jitterentropy-base.c | grep define | grep MAJVERSION | awk '{print $$3}')
LIBMINOR=$(shell cat jitterentropy-base.c | grep define | grep MINVERSION | awk '{print $$3}')
LIBPATCH=$(shell cat jitterentropy-base.c | grep define | grep PATCHLEVEL | awk '{print $$3}')
LIBVERSION := $(LIBMAJOR).$(LIBMINOR).$(LIBPATCH)

#C_SRCS := $(wildcard *.c) 
C_SRCS := jitterentropy-base.c
C_OBJS := ${C_SRCS:.c=.o}
OBJS := $(C_OBJS)

INCLUDE_DIRS :=
LIBRARY_DIRS :=
LIBRARIES := rt

CFLAGS += $(foreach includedir,$(INCLUDE_DIRS),-I$(includedir))
LDFLAGS += $(foreach librarydir,$(LIBRARY_DIRS),-L$(librarydir))
LDFLAGS += $(foreach library,$(LIBRARIES),-l$(library))

.PHONY: all scan install clean distclean

all: $(NAME)

$(NAME): $(OBJS)
	$(CC) -shared -Wl,-soname,lib$(NAME).so.$(LIBMAJOR) -o lib$(NAME).so.$(LIBVERSION) $(OBJS) $(LDFLAGS)

scan:	$(OBJS)
	scan-build --use-analyzer=/usr/bin/clang $(CC) -shared -Wl,-soname,lib$(NAME).so.$(LIBMAJOR) -o lib$(NAME).so.$(LIBVERSION) $(OBJS) $(LDFLAGS)

install:
	install -m 644 doc/$(NAME).3 $(PREFIX)/share/man/man3/
	gzip -9 $(PREFIX)/share/man/man3/$(NAME).3
	install -m 0755 -s lib$(NAME).so.$(LIBVERSION) $(PREFIX)/$(LIBDIR)/
	$(RM) $(PREFIX)/$(LIBDIR)/lib$(NAME).so.$(LIBMAJOR)
	ln -s lib$(NAME).so.$(LIBVERSION) $(PREFIX)/$(LIBDIR)/lib$(NAME).so.$(LIBMAJOR)

clean:
	@- $(RM) $(NAME)
	@- $(RM) $(OBJS)
	@- $(RM) lib$(NAME).so*

distclean: clean

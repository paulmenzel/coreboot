##
## This file is part of the coreinfo project.
##
## Copyright (C) 2008 Advanced Micro Devices, Inc.
## Copyright (C) 2008 Uwe Hermann <uwe@hermann-uwe.de>
##
## This program is free software; you can redistribute it and/or modify
## it under the terms of the GNU General Public License as published by
## the Free Software Foundation; version 2 of the License.
##
## This program is distributed in the hope that it will be useful,
## but WITHOUT ANY WARRANTY; without even the implied warranty of
## MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
## GNU General Public License for more details.
##

src := $(CURDIR)
srctree := $(src)
srck := $(src)/../../util/kconfig
cbui_obj := $(src)/build
objk := $(src)/build/util/kconfig

nuklear_git_repo=git@github.com:siro20/nuklear.git
nuklear_dir=nuklear
NUKLEAR_HEADERS=$(nuklear_dir)/demo/x11_rawfb/nuklear_rawfb.h $(nuklear_dir)/nuklear.h

export KERNELVERSION      := 0.1.0
export KCONFIG_AUTOHEADER := $(cbui_obj)/config.h
export KCONFIG_AUTOCONFIG := $(cbui_obj)/auto.conf
export KCONFIG_DEPENDENCIES := $(cbui_obj)/auto.conf.cmd
export KCONFIG_SPLITCONFIG := $(cbui_obj)/config
export KCONFIG_TRISTATE := $(cbui_obj)/tristate.conf
export KCONFIG_CONFIG := $(CURDIR)/.config
export KCONFIG_NEGATIVES := 1
export Kconfig := Kconfig

export V := $(V)

CONFIG_SHELL := sh
KBUILD_DEFCONFIG := configs/defconfig
UNAME_RELEASE := $(shell uname -r)
HAVE_DOTCONFIG := $(wildcard .config)
MAKEFLAGS += -rR --no-print-directory

# Make is silent per default, but 'make V=1' will show all compiler calls.
ifneq ($(V),1)
.SILENT:
endif

HOSTCC ?= gcc
HOSTCXX ?= g++
HOSTCFLAGS := -I$(srck) -I$(objk)
HOSTCXXFLAGS := -I$(srck) -I$(objk)

LIBPAYLOAD_PATH := $(realpath ../libpayload)
LIBPAYLOAD_OBJ := $(cbui_obj)/libpayload
HAVE_LIBPAYLOAD := $(wildcard $(LIBPAYLOAD_OBJ)/lib/libpayload.a)
LIBPAYLOAD_CONFIG := $(PWD)/lp.config
OBJCOPY ?= objcopy

INCLUDES = -I$(cbui_obj) -include $(LIBPAYLOAD_OBJ)/include/kconfig.h -I$(src)/../../src/commonlib/include
OBJECTS = cpuinfo_module.o pci_module.o cbui.o  nvram_module.o bootlog_module.o cbfs_module.o timestamps_module.o \
cmos_module.o splash.o coreboot_module.o cpuid.o rtc_module.o memcpy.o

OBJS    = $(patsubst %,$(cbui_obj)/%,$(OBJECTS))
TARGET  = $(cbui_obj)/cbui.elf

all: real-all

# in addition to the dependency below, create the file if it doesn't exist
# to silence warnings about a file that would be generated anyway.
$(if $(wildcard .xcompile),,$(eval $(shell ../../util/xcompile/xcompile $(XGCCPATH) > .xcompile || rm -f .xcompile)))
.xcompile: ../../util/xcompile/xcompile
	$< $(XGCCPATH) > $@.tmp
	\mv -f $@.tmp $@ 2> /dev/null || rm -f $@.tmp $@

CONFIG_COMPILER_GCC := y
ARCH-y     := x86_32

include .xcompile

CC := $(CC_$(ARCH-y))
AS := $(AS_$(ARCH-y))
OBJCOPY := $(OBJCOPY_$(ARCH-y))

LPCC := CC="$(CC)" $(LIBPAYLOAD_OBJ)/bin/lpgcc
LPAS := AS="$(AS)" $(LIBPAYLOAD_OBJ)/bin/lpas

CFLAGS_x86_32 += -msse

CFLAGS += -Wall -Wno-unused-function -O2 -fno-builtin $(CFLAGS_$(ARCH-y)) $(INCLUDES) -Wno-error=format= -DRAWFB_XRGB_8888=1

ifneq ($(strip $(HAVE_DOTCONFIG)),)
include $(src)/.config
real-all: $(TARGET)

$(TARGET): $(src)/.config $(cbui_obj)/config.h $(OBJS) libpayload
	printf "    LPCC       $(subst $(CURDIR)/,,$(@)) (LINK)\n"
	$(LPCC) -o $@ $(OBJS)
	$(OBJCOPY) --only-keep-debug $@ $(TARGET).debug
	$(OBJCOPY) --strip-debug $@
	$(OBJCOPY) --add-gnu-debuglink=$(TARGET).debug $@

$(cbui_obj)/%.S.o: $(src)/%.S libpayload
	printf "    LPAS       $(subst $(CURDIR)/,,$(@))\n"
	$(LPAS) -o $@ $<

$(cbui_obj)/%.o: $(src)/%.c libpayload $(NUKLEAR_HEADERS)
	printf "    LPCC       $(subst $(CURDIR)/,,$(@))\n"
	$(LPCC) $(CFLAGS) -c -o $@ $<

else
real-all: config
endif

defaultbuild:
	$(MAKE) olddefconfig
	$(MAKE) all

ifneq ($(strip $(HAVE_LIBPAYLOAD)),)
libpayload:
	printf "Found Libpayload $(LIBPAYLOAD_OBJ).\n"
else
LPOPTS=obj="$(CURDIR)/lpbuild" DOTCONFIG="$(CURDIR)/lp.config"
libpayload:
	printf "Building libpayload @ $(LIBPAYLOAD_PATH).\n"
	$(MAKE) -C $(LIBPAYLOAD_PATH) $(LPOPTS) distclean cbui_obj=$(cbui_obj)/libptmp
	$(MAKE) -C $(LIBPAYLOAD_PATH) $(LPOPTS) defconfig KBUILD_DEFCONFIG=$(LIBPAYLOAD_CONFIG)
	$(MAKE) -C $(LIBPAYLOAD_PATH) $(LPOPTS) install DESTDIR=$(cbui_obj)
endif

checkout:
	echo "    GIT        NUKLEAR"
	test -d $(nuklear_dir) || \
		git clone $(nuklear_git_repo) $(nuklear_dir)
	cd $(nuklear_dir) && \
	git checkout master && \
	git pull;

$(nuklear_dir)/demo/x11_rawfb/nuklear_rawfb.h: checkout
$(nuklear_dir)/nuklear.h: checkout

$(cbui_obj)/config.h:
	$(MAKE) oldconfig

$(shell mkdir -p $(cbui_obj) $(objk)/lxdialog $(KCONFIG_SPLITCONFIG))

clean:
	rm -rf build/*.elf build/*.o .xcompile

distclean: clean
	rm -rf build lpbuild
	rm -f .config* lp.config*

include $(srck)/Makefile

.PHONY: $(PHONY) prepare clean distclean checkout

# SPDX-License-Identifier: BSD-2-Clause
# SPDX-FileCopyrightText: 2025 Stefan Reinauer
#
# xSysInfo Makefile for GCC (m68k-amigaos)

ADATE   := $(shell date '+%-d.%-m.%Y')
# FULL_VERSION is 42.xx-yy-dirty
FULL_VERSION ?= $(shell git describe --tags --dirty | sed -r 's/^release_//')
PROG_VERSION := $(shell echo $(FULL_VERSION) | cut -f1 -d\.)
PROG_REVISION := $(shell echo $(FULL_VERSION) | cut -f2 -d\.|cut -f1 -d\-)

CC = m68k-amigaos-gcc
STRIP = m68k-amigaos-strip
VASM    := vasmm68k_mot

# NDK include path (override with: make NDK_INC=/your/path)
NDK_INC ?= /opt/amiga/m68k-amigaos/ndk-include

# Include paths: our includes + identify.library reference includes
IDENTIFY_INC = 3rdparty/identify/reference

# Include paths: our includes + identify.library reference includes
IDENTIFY_INC = 3rdparty/identify/reference

CFLAGS = -O2 -m68000 -mtune=68020-60 -Wa,-m68881 -msoft-float -noixemul -Wall -Wextra \
         -I$(IDENTIFY_INC) \
         -DXSYSINFO_DATE="\"$(ADATE)\"" -DXSYSINFO_VERSION="\"$(FULL_VERSION)\"" \
         -DPROG_VERSION=$(PROG_VERSION) -DPROG_REVISION=$(PROG_REVISION)

LDFLAGS = -noixemul
LIBS = -lamiga -lgcc

# Source files
SRCS = src/main.c \
       src/gui.c \
       src/hardware.c \
       src/benchmark.c \
       src/dhry_1.c \
       src/dhry_2.c \
       src/memory.c \
       src/drives.c \
       src/scsi.c \
       src/boards.c \
       src/software.c \
       src/cache.c \
       src/print.c \
       src/locale.c

OBJS = $(SRCS:.c=.o)

TARGET = xSysInfo

.PHONY: all clean identify catalogs

# Detect platform for flexcat binary path
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
    FLEXCAT_BIN = 3rdparty/flexcat/src/bin_darwin/flexcat
else
    FLEXCAT_BIN = 3rdparty/flexcat/src/bin_unix/flexcat
endif

all: identify $(TARGET) disk

# FlexCat build - only when binary doesn't exist
$(FLEXCAT_BIN):
	@$(MAKE) -s -C 3rdparty/flexcat bootstrap
	@$(MAKE) -s -C 3rdparty/flexcat

# Identify library build (requires FlexCat)
identify: $(FLEXCAT_BIN)
	@{ if [ ! -f $(HOME)/.fd2pragma.types ]; then \
	     echo '$$(HOME)/.fd2pragma.types not found. Downloading.'; \
	     curl -sL 'https://github.com/adtools/fd2pragma/raw/refs/heads/master/fd2pragma.types' \
		  -o $(HOME)/.fd2pragma.types; \
	   fi } && \
	export PATH="$(CURDIR)/3rdparty/flexcat/src/bin_unix:$(CURDIR)/3rdparty/flexcat/src/bin_darwin:$(PATH)" && \
	$(MAKE) -s -C 3rdparty/identify reference/proto/identify.h reference/inline/identify.h

# Catalog definitions - maps source directory to AmigaOS language name
CATALOG_DESC = catalogs/xSysInfo.cd
CATALOG_DIR = catalogs/build
CATALOG_LANGS = german:deutsch french:français turkish:türkçe polish:polski

# Build all catalogs
catalogs: $(FLEXCAT_BIN)
	@for mapping in $(CATALOG_LANGS); do \
		src=$${mapping%%:*}; \
		lang=$${mapping##*:}; \
		mkdir -p "$(CATALOG_DIR)/$$lang"; \
		echo "  CATALOG $$lang"; \
		$(FLEXCAT_BIN) $(CATALOG_DESC) "catalogs/$$src/xSysInfo.ct" CATALOG "$(CATALOG_DIR)/$$lang/xSysInfo.catalog"; \
	done

$(TARGET): $(OBJS)
	@echo "  LINK  $@"
	@$(CC) $(LDFLAGS) -o $@ $(OBJS) $(LIBS)
	@echo "  STRIP $@"
	@$(STRIP) $@
	@wc -c < "$@" | awk '{printf "$@ successfully compiled (%s bytes)\n", $$1}'

src/%.o: src/%.c src/xsysinfo.h
	@echo "  CC    $@"
	@$(CC) $(CFLAGS) -c -o $@ $<

clean:
	@echo "  CLEAN"
	@rm -f $(OBJS) $(TARGET) TinySetPatch
	@rm -rf $(CATALOG_DIR)
	@$(MAKE) -s -C 3rdparty/flexcat clean
	@$(MAKE) -s -C 3rdparty/identify clean

# Dependencies
src/main.o: src/main.c src/xsysinfo.h src/gui.h src/hardware.h src/locale_str.h
src/gui.o: src/gui.c src/xsysinfo.h src/gui.h src/hardware.h src/benchmark.h src/locale_str.h
src/hardware.o: src/hardware.c src/xsysinfo.h src/hardware.h
src/benchmark.o: src/benchmark.c src/xsysinfo.h src/benchmark.h
src/memory.o: src/memory.c src/xsysinfo.h src/memory.h src/locale_str.h
src/drives.o: src/drives.c src/xsysinfo.h src/drives.h src/scsi.h src/locale_str.h
src/scsi.o: src/scsi.c src/xsysinfo.h src/scsi.h src/gui.h src/locale_str.h
src/boards.o: src/boards.c src/xsysinfo.h src/boards.h src/locale_str.h
src/software.o: src/software.c src/xsysinfo.h src/software.h
src/cache.o: src/cache.c src/xsysinfo.h src/cache.h
src/print.o: src/print.c src/xsysinfo.h src/print.h src/hardware.h src/software.h
src/locale.o: src/locale.c src/xsysinfo.h src/locale_str.h
src/dhry_1.o: src/dhry_1.c src/dhry.h
src/dhry_2.o: src/dhry_2.c src/dhry.h

# Disk creation
DISK = xsysinfo-$(FULL_VERSION).adf

# Downloads directory and files
DOWNLOAD_DIR = downloads
IDENTIFY_USR_LHA = $(DOWNLOAD_DIR)/IdentifyUsr.lha
IDENTIFY_PCI_LHA = $(DOWNLOAD_DIR)/IdentifyPci.lha
OPENPCI_LHA = $(DOWNLOAD_DIR)/openpci68k.lha
MMULIB_LHA = $(DOWNLOAD_DIR)/MMULib.lha

# MD5 checksums for verification
IDENTIFY_USR_MD5 = f8bd9feb9fa595bea979755224d08c5c
IDENTIFY_PCI_MD5 = 7771426e5c7a5e3dc882a973029099d1
OPENPCI_MD5 = e8f18f6c5111479756294576822eaf98
MMULIB_MD5 = 5d07a2dc0f495a9c6790fa7d1df43f1d

.PHONY: identify-all disk download-libs

# Create downloads directory
$(DOWNLOAD_DIR):
	mkdir -p $(DOWNLOAD_DIR)

# Portable MD5 verification (works on Linux and macOS)
# Usage: $(call verify_md5,file,expected_md5)
# Returns 0 (success) if match, 1 (failure) if mismatch
define md5_cmd
md5sum "$(1)" 2>/dev/null | cut -d' ' -f1 || md5 -q "$(1)" 2>/dev/null
endef
define verify_md5_cmd
actual=$$( $(call md5_cmd,$(1)) ); \
[ "$$actual" = "$(2)" ]
endef
define md5_fail_msg
actual=$$( $(call md5_cmd,$(1)) ); \
echo "$(1): FAILED (MD5 mismatch)"; \
echo "Expected MD5: $(2)"; \
echo "Got MD5: $$actual"
endef

# Download and verify IdentifyUsr.lha
$(IDENTIFY_USR_LHA): | $(DOWNLOAD_DIR)
	@if [ -f "$@" ] && $(call verify_md5_cmd,$@,$(IDENTIFY_USR_MD5)); then \
		echo "$@ already downloaded and verified"; \
	else \
		echo "Downloading IdentifyUsr.lha..."; \
		curl -sL http://aminet.net/util/libs/IdentifyUsr.lha -o $@; \
		if $(call verify_md5_cmd,$@,$(IDENTIFY_USR_MD5)); then \
			echo "$@: OK"; \
		else \
			$(call md5_fail_msg,$@,$(IDENTIFY_USR_MD5)); rm -f $@; exit 1; \
		fi \
	fi

# Download and verify IdentifyPci.lha
$(IDENTIFY_PCI_LHA): | $(DOWNLOAD_DIR)
	@if [ -f "$@" ] && $(call verify_md5_cmd,$@,$(IDENTIFY_PCI_MD5)); then \
		echo "$@ already downloaded and verified"; \
	else \
		echo "Downloading IdentifyPci.lha..."; \
		curl -sL http://aminet.net/util/libs/IdentifyPci.lha -o $@; \
		if $(call verify_md5_cmd,$@,$(IDENTIFY_PCI_MD5)); then \
			echo "$@: OK"; \
		else \
			$(call md5_fail_msg,$@,$(IDENTIFY_PCI_MD5)); rm -f $@; exit 1; \
		fi \
	fi

# Download and verify openpci68k.lha
$(OPENPCI_LHA): | $(DOWNLOAD_DIR)
	@if [ -f "$@" ] && $(call verify_md5_cmd,$@,$(OPENPCI_MD5)); then \
		echo "$@ already downloaded and verified"; \
	else \
		echo "Downloading openpci68k.lha..."; \
		curl -sL http://aminet.net/driver/other/openpci68k.lha -o $@; \
		if $(call verify_md5_cmd,$@,$(OPENPCI_MD5)); then \
			echo "$@: OK"; \
		else \
			$(call md5_fail_msg,$@,$(OPENPCI_MD5)); rm -f $@; exit 1; \
		fi \
	fi

# Download and verify MMULib.lha
$(MMULIB_LHA): | $(DOWNLOAD_DIR)
	@if [ -f "$@" ] && $(call verify_md5_cmd,$@,$(MMULIB_MD5)); then \
		echo "$@ already downloaded and verified"; \
	else \
		echo "Downloading MMULib.lha..."; \
		curl -sL http://aminet.net/util/libs/MMULib.lha -o $@; \
		if $(call verify_md5_cmd,$@,$(MMULIB_MD5)); then \
			echo "$@: OK"; \
		else \
			$(call md5_fail_msg,$@,$(MMULIB_MD5)); rm -f $@; exit 1; \
		fi \
	fi

# Download all libraries
download-libs: $(IDENTIFY_USR_LHA) $(IDENTIFY_PCI_LHA) $(OPENPCI_LHA) $(MMULIB_LHA)
	@mkdir -p 3rdparty/identify/build
	# Extract Identify library (use 68000-compatible version)
	@echo "  UNPACK $(IDENTIFY_USR_LHA)"
	@lha xq $(IDENTIFY_USR_LHA) Identify/libs/identify.library_000
	@mv Identify/libs/identify.library_000 3rdparty/identify/build/identify.library
	@rm -rf Identify
	# Extract PCI database
	@echo "  UNPACK $(IDENTIFY_PCI_LHA)"
	@lha xq $(IDENTIFY_PCI_LHA) Identify/s/pci.db
	@mv Identify/s/pci.db 3rdparty/identify/build/
	@rm -rf Identify
	# Extract OpenPCI library
	@echo "  UNPACK $(OPENPCI_LHA)"
	@lha xq $(OPENPCI_LHA) Libs/openpci.library
	@mv Libs/openpci.library 3rdparty/identify/build/
	@rm -rf Libs
	# Extract MMULib
	@echo "  UNPACK $(MMULIB_LHA)"
	@lha xq $(MMULIB_LHA) MMULib/Libs/mmu.library \
		MMULib/Libs/680x0.library MMULib/Libs/68020.library \
		MMULib/Libs/68030.library MMULib/Libs/68040.library \
		MMULib/Libs/68060.library
	@mv MMULib/Libs/mmu.library 3rdparty/identify/build/
	@mv MMULib/Libs/680*.library 3rdparty/identify/build/
	@rm -rf MMULib

TinySetPatch: src/TinySetPatch.S
	@echo "  VASM $@"
	@$(VASM) -quiet -Fhunkexe -o $@ -nosym $< -I $(NDK_INC)

disk: $(TARGET) download-libs TinySetPatch
	@echo "  DISK"
	@xdftool $(DISK) format "xSysInfo"
	@xdftool $(DISK) write $(TARGET) $(TARGET)
	@xdftool $(DISK) write docs/$(TARGET).info $(TARGET).info
	@xdftool $(DISK) write docs/Disk.info Disk.info
	@xdftool $(DISK) makedir Libs
	@xdftool $(DISK) write 3rdparty/identify/build/identify.library Libs/identify.library
	@xdftool $(DISK) write 3rdparty/identify/build/openpci.library Libs/openpci.library
	@for lib in mmu 680x0 68020 68030 68040 68060; do \
		xdftool $(DISK) write 3rdparty/identify/build/$$lib.library Libs/$$lib.library; \
	done
	@xdftool $(DISK) makedir S
	@xdftool $(DISK) write Startup-Sequence S/Startup-Sequence
	@xdftool $(DISK) write 3rdparty/identify/build/pci.db S/pci.db
	@xdftool $(DISK) makedir C
	@xdftool $(DISK) write TinySetPatch C/TinySetPatch
	@xdftool $(DISK) boot install
	@xdftool $(DISK) info
	@ln -sf $(DISK) xsysinfo.adf

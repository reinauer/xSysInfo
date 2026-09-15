# SPDX-License-Identifier: BSD-2-Clause
# SPDX-FileCopyrightText: 2025 Stefan Reinauer
#
# xSysInfo Makefile for GCC (m68k-amigaos)

ADATE   := $(shell date '+%-d.%-m.%Y')
# FULL_VERSION is 42.xx-yy-dirty
FULL_VERSION ?= $(shell git describe --tags --dirty | sed -E 's/^release_//; s/^v//')
PROG_VERSION := $(shell echo $(FULL_VERSION) | cut -f1 -d\.)
PROG_REVISION := $(shell echo $(FULL_VERSION) | cut -f2 -d\.|cut -f1 -d\-)

CC = m68k-amigaos-gcc
STRIP = m68k-amigaos-strip
VASM    := vasmm68k_mot

# NDK include path (override with: make NDK_PATH=/your/path)
NDK_PATH ?= $(shell realpath $$(dirname $$(which $(CC)))/../m68k-amigaos/ndk-include)

# Cached MMU runtime libraries and developer files
DOWNLOAD_DIR = downloads
MMULIB_LHA = $(DOWNLOAD_DIR)/MMULib.lha
MU_MANUAL_LHA = $(DOWNLOAD_DIR)/MuManual.lha
MMU_DIR = $(DOWNLOAD_DIR)/MuManual
MMU_LIB_DIR = $(DOWNLOAD_DIR)/MMULib/Libs
MMU_LIB_NAMES = mmu 68020 68030 68040 68060
MMU_LIBS = $(addprefix $(MMU_LIB_DIR)/,$(addsuffix .library,$(MMU_LIB_NAMES)))
FD2PRAGMA_TYPES = $(HOME)/.fd2pragma.types

# Include paths for the downloaded developer files
IDENTIFY_INC = 3rdparty/identify/reference
MMU_INC = $(MMU_DIR)/Include

#LTO ?= -flto=auto
CFLAGS = -Os -m68000 -mtune=68020-60 -Wa,-m68881 -msoft-float -noixemul -Wall -Wextra \
         -I$(IDENTIFY_INC) \
         -I$(MMU_INC) \
         -DXSYSINFO_DATE="\"$(ADATE)\"" -DXSYSINFO_VERSION="\"$(FULL_VERSION)\"" \
         -DPROG_VERSION=$(PROG_VERSION) -DPROG_REVISION=$(PROG_REVISION) $(LTO)

ASMFLAGS = -Fhunk -esc -sc -m68020up -I $(NDK_PATH)
# Select the Kick 1.3-safe libnix runtime.  This keeps the no-ixemul build
# while avoiding libnix20 helpers that auto-open utility.library.
LDFLAGS = -mcrt=nix13 $(LTO)
LIBS = -lamiga -lgcc

# Source files
SRCS = src/main.c \
       src/gui.c \
       src/battmem.c \
       src/hardware.c \
       src/benchmark.c \
       src/busclock.c \
       src/dhry_1.c \
       src/dhry_2.c \
       src/memory.c \
       src/drives.c \
       src/scsi.c \
       src/boards.c \
       src/software.c \
       src/cache.c \
       src/print.c \
       src/which.c \
       src/locale.c

ASM_SRCS = src/cpu.S src/berr_trap.S
TINYSETPATCH_DIR = 3rdparty/TinySetPatch
TINYSETPATCH_SRC = $(TINYSETPATCH_DIR)/TinySetPatch.S
TINYSETPATCH_BIN = $(TINYSETPATCH_DIR)/TinySetPatch
STACK = Stack
STACK_SRC = src/Stack.c
STACK_OBJ = src/Stack.o
STACK_CFLAGS = $(filter-out -flto%,$(CFLAGS)) -fno-lto

OBJS = $(SRCS:.c=.o)

BENCH_OBJS = src/benchmark.o src/dhry_1.o src/dhry_2.o

ASM_OBJS = $(ASM_SRCS:.S=.o)

TARGET = xSysInfo

.PHONY: all clean identify mmu catalogs lha TinySetPatch

# FlexCat uses the Unix target on both Linux and macOS.
FLEXCAT_BIN = 3rdparty/flexcat/src/bin_unix/flexcat

all: download-libs identify mmu $(TARGET) disk lha

# FlexCat build - only when binary doesn't exist
$(FLEXCAT_BIN):
	@$(MAKE) -s -C 3rdparty/flexcat bootstrap
	@$(MAKE) -s -C 3rdparty/flexcat

# Identify library build (requires FlexCat)
IDENTIFY_HEADER = $(IDENTIFY_INC)/proto/identify.h

identify: $(IDENTIFY_HEADER)

$(FD2PRAGMA_TYPES):
	@echo "  DOWNLOAD $@"
	@curl -fLsS 'https://github.com/adtools/fd2pragma/raw/refs/heads/master/fd2pragma.types' -o $@

$(IDENTIFY_HEADER): $(FLEXCAT_BIN) $(FD2PRAGMA_TYPES) | download-libs
	@export PATH="$(CURDIR)/$(dir $(FLEXCAT_BIN)):$(PATH)" && \
	$(MAKE) -s -C 3rdparty/identify reference/proto/identify.h reference/inline/identify.h

# Generate compiler bindings directly beside the MuManual include files.
MMU_HEADERS = $(MMU_INC)/proto/mmu.h $(MMU_INC)/inline/mmu.h \
	$(MMU_INC)/inline/mmu_protos.h
MMU_SDK_FILES = $(MMU_DIR)/fd/mmu_lib.fd $(MMU_DIR)/fd/mmu_resource.fd \
	$(MMU_INC)/clib/mmu_protos.h $(MMU_INC)/pragmas/mmu_pragmas.h \
	$(addprefix $(MMU_INC)/mmu/,alerts.h config.h context.h descriptor.h \
		exceptions.h mmubase.h mmutags.h)

mmu: $(MMU_HEADERS)

$(MMU_INC)/proto/mmu.h: MMU_HEADER_TYPE = 38
$(MMU_INC)/inline/mmu.h: MMU_HEADER_TYPE = 40
$(MMU_INC)/inline/mmu_protos.h: MMU_HEADER_TYPE = 70

$(MMU_HEADERS): $(MMU_SDK_FILES) $(FD2PRAGMA_TYPES)
	@echo "  HEADER $@"
	@mkdir -p $(dir $@)
	@fd2pragma --infile $(MMU_DIR)/fd/mmu_lib.fd \
		--clib $(MMU_INC)/clib/mmu_protos.h --to $(dir $@) \
		--special $(MMU_HEADER_TYPE) --autoheader --comment

# Catalog definitions - maps source directory to AmigaOS language name
CATALOG_DESC = catalogs/xSysInfo.cd
CATALOG_DIR = catalogs/build
CATALOG_LANGS = german:deutsch french:français italian:italiano \
		turkish:türkçe polish:polski portuguese:português \
		hungarian:magyar

# Build all catalogs
catalogs: $(FLEXCAT_BIN)
	@for mapping in $(CATALOG_LANGS); do \
		src=$${mapping%%:*}; \
		lang=$${mapping##*:}; \
		mkdir -p "$(CATALOG_DIR)/$$lang"; \
		echo "  CATALOG $$lang"; \
		$(FLEXCAT_BIN) $(CATALOG_DESC) "catalogs/$$src/xSysInfo.ct" CATALOG "$(CATALOG_DIR)/$$lang/xSysInfo.catalog"; \
	done

# LHA archive creation
LHA_NAME = xsysinfo-$(FULL_VERSION).lha
LHA_DIR = xSysInfo-$(FULL_VERSION)
LHA_OPTS := $(shell if lha 2>&1 | grep 'archive-kanji-code' | grep -q 'latin1'; then echo '--system-kanji-code=utf8 --archive-kanji-code=latin1'; fi)

lha: $(TARGET) TinySetPatch catalogs
	@echo "  LHA   $(LHA_NAME)"
	@rm -rf $(LHA_DIR)
	@mkdir -p $(LHA_DIR)
	@cp $(TARGET) $(LHA_DIR)/
	@cp TinySetPatch $(LHA_DIR)/
	@cp docs/readme.txt $(LHA_DIR)/
	@cp docs/xSysInfo.info $(LHA_DIR)/
	@cp LICENSE $(LHA_DIR)/
	@for catalog in $(CATALOG_DIR)/*/xSysInfo.catalog; do \
		lang=$$(basename $$(dirname "$$catalog")); \
		cp "$$catalog" "$(LHA_DIR)/xSysInfo_$$lang.catalog"; \
	done
	@cp docs/dir.info $(LHA_DIR).info
	@lha aqo5 $(LHA_OPTS) $(LHA_NAME) $(LHA_DIR) $(LHA_DIR).info
	@rm -rf $(LHA_DIR) $(LHA_DIR).info
	@echo "Created $(LHA_NAME)"

$(TARGET): $(OBJS) $(ASM_OBJS)
	@echo "  LINK  $@"
	@$(CC) $(LDFLAGS) -o $@ $(OBJS) $(ASM_OBJS) $(LIBS)
	@echo "  STRIP $@"
	@$(STRIP) $@
	@wc -c < "$@" | awk '{printf "$@ successfully compiled (%s bytes)\n", $$1}'

$(STACK): $(STACK_OBJ)
	@echo "  LINK  $@"
	@$(CC) -nostartfiles -nostdlib -o $@ $(STACK_OBJ)
	@echo "  STRIP $@"
	@$(STRIP) $@
	@wc -c < "$@" | awk '{printf "$@ successfully compiled (%s bytes)\n", $$1}'

$(STACK_OBJ): $(STACK_SRC)
	@echo "  CC    $@"
	@$(CC) $(STACK_CFLAGS) -c -o $@ $<

$(OBJS): src/%.o: src/%.c src/xsysinfo.h $(IDENTIFY_HEADER) $(MMU_HEADERS)
	@echo "  CC    $@"
	@$(CC) $(CFLAGS) -c -o $@ $<

$(BENCH_OBJS): CFLAGS += -O2

$(ASM_OBJS): src/%.o: src/%.S
	@echo "  ASM   $@"
	@$(VASM) $(ASMFLAGS) -o $@ $<

clean:
	@echo "  CLEAN"
	@rm -f $(OBJS) $(ASM_OBJS) $(STACK_OBJ) $(TARGET) TinySetPatch $(STACK)
	@rm -rf $(CATALOG_DIR)
	@rm -f xsysinfo-*.lha
	@$(MAKE) -s -C 3rdparty/flexcat clean
	@$(MAKE) -s -C 3rdparty/identify clean
	@rm -rf $(MMU_DIR) $(DOWNLOAD_DIR)/MMULib

# Dependencies
src/main.o: src/main.c src/xsysinfo.h src/gui.h src/hardware.h src/which.h src/software.h src/memory.h src/boards.h src/benchmark.h src/busclock.h src/locale_str.h
src/gui.o: src/gui.c src/bayer-16x16.c src/xsysinfo.h src/gui.h src/hardware.h src/benchmark.h src/software.h src/memory.h src/locale_str.h
src/hardware.o: src/hardware.c src/xsysinfo.h src/hardware.h src/benchmark.h
src/benchmark.o: src/benchmark.c src/xsysinfo.h src/benchmark.h src/hardware.h
src/busclock.o: src/busclock.c src/busclock.h src/hardware.h src/cpu.h src/debug.h
src/memory.o: src/memory.c src/xsysinfo.h src/memory.h src/hardware.h src/locale_str.h
src/drives.o: src/drives.c src/xsysinfo.h src/drives.h src/scsi.h src/hardware.h src/locale_str.h
src/scsi.o: src/scsi.c src/xsysinfo.h src/scsi.h src/gui.h src/locale_str.h
src/boards.o: src/boards.c src/xsysinfo.h src/boards.h src/locale_str.h
src/software.o: src/software.c src/xsysinfo.h src/software.h src/hardware.h src/locale_str.h
src/cache.o: src/cache.c src/xsysinfo.h src/cache.h src/hardware.h
src/print.o: src/print.c src/xsysinfo.h src/print.h src/hardware.h src/software.h src/memory.h
src/which.o: src/which.c src/xsysinfo.h src/which.h src/hardware.h src/boards.h src/software.h src/memory.h
src/locale.o: src/locale.c src/xsysinfo.h src/locale_str.h
src/dhry_1.o: src/dhry_1.c src/dhry.h
src/dhry_2.o: src/dhry_2.c src/dhry.h

# Disk creation
DISK = xsysinfo-$(FULL_VERSION).adf
DISK_TITLE = $(shell printf '%s' "xSysInfo-$(FULL_VERSION)" | \
	sed 's/-dirty$$//' | cut -c1-30)

# Downloads directory and files
IDENTIFY_USR_LHA = $(DOWNLOAD_DIR)/IdentifyUsr.lha
IDENTIFY_PCI_LHA = $(DOWNLOAD_DIR)/IdentifyPci.lha
OPENPCI_LHA = $(DOWNLOAD_DIR)/openpci68k.lha

# MD5 checksums for verification
IDENTIFY_USR_MD5 = f8bd9feb9fa595bea979755224d08c5c
IDENTIFY_PCI_MD5 = 7771426e5c7a5e3dc882a973029099d1
OPENPCI_MD5 = 16aeac58eb66bde1e5c2dc47502ffa05
MMULIB_MD5 = 1e63e42c9d2895d22f896b6d90c26353
MU_MANUAL_MD5 = 98ce060266ec1ac2dece921f431253b1

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

# Download and verify MuManual.lha
$(MU_MANUAL_LHA): | $(DOWNLOAD_DIR)
	@if [ -f "$@" ] && $(call verify_md5_cmd,$@,$(MU_MANUAL_MD5)); then \
		echo "$@ already downloaded and verified"; \
	else \
		echo "Downloading MuManual.lha..."; \
		curl -sL http://aminet.net/docs/misc/MuManual.lha -o $@; \
		if $(call verify_md5_cmd,$@,$(MU_MANUAL_MD5)); then \
			echo "$@: OK"; \
		else \
			$(call md5_fail_msg,$@,$(MU_MANUAL_MD5)); rm -f $@; exit 1; \
		fi \
	fi
# Extract individual MMU files once, retaining the archives' directory layout.
# Refresh timestamps because archive members predate the downloaded archive.
$(MMU_LIBS): $(MMULIB_LHA)
	@echo "  UNPACK $@"
	@lha xqfw=$(DOWNLOAD_DIR) $< $(patsubst $(DOWNLOAD_DIR)/%,%,$@)
	@touch $@

$(MMU_SDK_FILES): $(MU_MANUAL_LHA)
	@echo "  UNPACK $@"
	@lha xqfw=$(DOWNLOAD_DIR) $< $(patsubst $(DOWNLOAD_DIR)/%,%,$@)
	@touch $@

# Download and prepare libraries and developer files.
download-libs: $(IDENTIFY_USR_LHA) $(IDENTIFY_PCI_LHA) $(OPENPCI_LHA) $(MMU_LIBS) $(MMU_SDK_FILES)
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

# Refresh TinySetPatch's own Git-derived version on every build. Parent
# command-line variables also enter the environment, so clear both paths
# through which xSysInfo's version overrides could reach the sub-build.
TinySetPatch: MAKEOVERRIDES =
TinySetPatch: $(TINYSETPATCH_SRC) $(TINYSETPATCH_DIR)/Makefile Makefile
	@env -u FULL_VERSION -u PROG_VERSION -u PROG_REVISION \
		$(MAKE) -s -C $(TINYSETPATCH_DIR) TinySetPatch \
		VASM=$(VASM) NDK_PATH="$(NDK_PATH)"
	@cp $(TINYSETPATCH_BIN) $@

disk: $(TARGET) download-libs TinySetPatch $(STACK)
	@echo "  DISK"
	@xdftool $(DISK) format "$(DISK_TITLE)"
	@xdftool $(DISK) write $(TARGET) $(TARGET)
	@xdftool $(DISK) write docs/$(TARGET).info $(TARGET).info
	@xdftool $(DISK) write docs/Disk.info Disk.info
	@xdftool $(DISK) makedir Libs
	@xdftool $(DISK) write 3rdparty/identify/build/identify.library Libs/identify.library
	@xdftool $(DISK) write 3rdparty/identify/build/openpci.library Libs/openpci.library
	@for lib in $(MMU_LIB_NAMES); do \
		xdftool $(DISK) write $(MMU_LIB_DIR)/$$lib.library Libs/$$lib.library; \
	done
	@xdftool $(DISK) makedir S
	@xdftool $(DISK) write Startup-Sequence S/Startup-Sequence
	@xdftool $(DISK) write 3rdparty/identify/build/pci.db S/pci.db
	@xdftool $(DISK) makedir C
	@xdftool $(DISK) write $(STACK) C/Stack
	@xdftool $(DISK) write TinySetPatch C/TinySetPatch
	@xdftool $(DISK) boot install
	@xdftool $(DISK) info
	@ln -sf $(DISK) xsysinfo.adf

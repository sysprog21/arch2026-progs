# rv32emu with system emulation and ELF_LOADER, fetched and built on demand.
#
# The first build fetches the latest rv32emu into third_party/ (a shallow
# clone of the default branch); it is not a git submodule and is never
# committed. `make update` fetches the newest commit again and rebuilds.
# `make emu-rev` prints the revision that would be fetched, for cache keys.
# The revision that produced a run is printed by `make cycles` and recorded in
# results/cycles.md. RV32EMU_REV=<commit-or-tag> pins a specific revision.
# The emulator's own CROSS_COMPILE is left empty: it names a guest compiler
# there, not the host one. SDL is switched off: the programs here need no
# display or audio, and the build then needs no SDL packages. The M, A, F and C
# extensions are switched off too: the emulator then cannot execute a
# multiply, however it is smuggled in (an encoding placed in data and jumped to
# passes any scan of the instructions), so the RV32I-only rule holds by
# construction and not only by inspection.

RV32EMU_REPO ?= https://github.com/sysprog21/rv32emu
RV32EMU_REV  ?=
RV32EMU_REF  := $(if $(strip $(RV32EMU_REV)),$(strip $(RV32EMU_REV)),HEAD)
RV32EMU_DIR  := third_party/rv32emu
RV32EMU_SRC  := $(RV32EMU_DIR)/.git/source-ready
EMU          := $(RV32EMU_DIR)/build/rv32emu
EMU_CONFIG   := $(RV32EMU_DIR)/build/.effective-config

$(RV32EMU_SRC):
	@mkdir -p $(RV32EMU_DIR)
	git init --quiet $(RV32EMU_DIR)
	git -C $(RV32EMU_DIR) config remote.origin.url $(RV32EMU_REPO)
	git -C $(RV32EMU_DIR) fetch --quiet --depth 1 origin $(RV32EMU_REF)
	git -C $(RV32EMU_DIR) checkout --quiet FETCH_HEAD
	@git -C $(RV32EMU_DIR) log -1 --format='rv32emu %h %cs %s'
	@touch $@

# The interpreter-only system configuration: no JIT, so runs are reproducible
# and the build needs no LLVM. The source tree is only a prerequisite for the
# first build: a finished build is not redone because the checkout is newer.
$(EMU): | $(RV32EMU_SRC)
	$(MAKE) -C $(RV32EMU_DIR) CROSS_COMPILE= distclean
	$(MAKE) -C $(RV32EMU_DIR) CROSS_COMPILE= system_interpreter_defconfig
	$(MAKE) -C $(RV32EMU_DIR) CROSS_COMPILE= ENABLE_ELF_LOADER=1 ENABLE_SDL=0 ENABLE_SDL_MIXER=0 \
	    ENABLE_EXT_M=0 ENABLE_EXT_A=0 ENABLE_EXT_F=0 ENABLE_EXT_C=0
	@grep -q '^CONFIG_SYSTEM=y' $(EMU_CONFIG) && \
	 grep -q '^CONFIG_ELF_LOADER=y' $(EMU_CONFIG) || \
	 { echo "rv32emu was not built with system emulation and ELF_LOADER"; \
	   rm -f $(EMU); exit 1; }

.PHONY: emu-rev update
emu-rev:
	@if echo '$(RV32EMU_REF)' | grep -Eq '^[0-9a-f]{40}$$'; then echo '$(RV32EMU_REF)'; \
	 else git ls-remote $(RV32EMU_REPO) $(RV32EMU_REF) | head -n 1 | cut -f1; fi

update: | $(RV32EMU_SRC)
	git -C $(RV32EMU_DIR) fetch --quiet --depth 1 origin $(RV32EMU_REF)
	git -C $(RV32EMU_DIR) reset --quiet --hard FETCH_HEAD
	@rm -f $(EMU)
	@$(MAKE) $(EMU)
	@git -C $(RV32EMU_DIR) log -1 --format='rv32emu %h %cs %s'

# rv32emu with system emulation and ELF_LOADER, fetched and built on demand.
#
# The first build clones the latest rv32emu into third_party/ (a shallow clone
# of the default branch); it is not a git submodule and is never committed.
# `make update` fetches the newest commit again and rebuilds. The revision
# that produced a run is printed by `make cycles` and recorded in
# results/cycles.md. RV32EMU_REV=<commit-or-tag> pins a specific revision.
# The emulator's own CROSS_COMPILE is left empty: it names a guest compiler
# there, not the host one.

RV32EMU_REPO ?= https://github.com/sysprog21/rv32emu
RV32EMU_REV  ?=
RV32EMU_DIR  := third_party/rv32emu
RV32EMU_SRC  := $(RV32EMU_DIR)/.git
EMU          := $(RV32EMU_DIR)/build/rv32emu
EMU_CONFIG   := $(RV32EMU_DIR)/build/.effective-config

$(RV32EMU_SRC):
	@mkdir -p third_party
ifeq ($(strip $(RV32EMU_REV)),)
	git clone --quiet --depth 1 $(RV32EMU_REPO) $(RV32EMU_DIR)
else
	git clone --quiet $(RV32EMU_REPO) $(RV32EMU_DIR)
	git -C $(RV32EMU_DIR) checkout --quiet $(RV32EMU_REV)
endif
	@git -C $(RV32EMU_DIR) log -1 --format='rv32emu %h %cs %s'

# The interpreter-only system configuration: no JIT, so runs are reproducible
# and the build needs no LLVM.
$(EMU): $(RV32EMU_SRC)
	$(MAKE) -C $(RV32EMU_DIR) CROSS_COMPILE= distclean
	$(MAKE) -C $(RV32EMU_DIR) CROSS_COMPILE= system_interpreter_defconfig
	$(MAKE) -C $(RV32EMU_DIR) CROSS_COMPILE= ENABLE_ELF_LOADER=1
	@grep -q '^CONFIG_SYSTEM=y' $(EMU_CONFIG) && \
	 grep -q '^CONFIG_ELF_LOADER=y' $(EMU_CONFIG) || \
	 { echo "rv32emu was not built with system emulation and ELF_LOADER"; \
	   rm -f $(EMU); exit 1; }

.PHONY: update
update: $(RV32EMU_SRC)
ifeq ($(strip $(RV32EMU_REV)),)
	git -C $(RV32EMU_DIR) fetch --quiet --depth 1 origin
	git -C $(RV32EMU_DIR) reset --quiet --hard FETCH_HEAD
else
	git -C $(RV32EMU_DIR) fetch --quiet origin
	git -C $(RV32EMU_DIR) checkout --quiet $(RV32EMU_REV)
endif
	@rm -f $(EMU)
	@$(MAKE) $(EMU)
	@git -C $(RV32EMU_DIR) log -1 --format='rv32emu %h %cs %s'

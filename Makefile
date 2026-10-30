# Bare-metal RV32I programs for rv32emu system emulation.
#
#   make              fetch the tools, build every ELF at -O1 -O2 -O3 -Os
#   make test         grade every assignment item (scripts/driver.py)
#   make cycles       measure, tabulate and plot the cycle counts
#   make sanitize     run the host-side tests under ASan and UBSan
#   make dump         write disassembly listings under build/
#   make size         per-implementation size -A report
#   make toolchain    validate the GNU toolchain
#   make indent       format with clang-format 20

PROGS := rubik bf16 gcd fixed
OPTS  := O1 O2 O3 Os

BUILD := build

include mk/toolchain.mk
include mk/rv32emu.mk

# Control the build verbosity
ifeq ("$(VERBOSE)","1")
    Q :=
    VECHO = @true
else
    Q := @
    VECHO = @printf
endif

GIT_HOOKS := .git/hooks/applied

.DEFAULT_GOAL := all
all: $(GIT_HOOKS) toolchain deps elfs

$(GIT_HOOKS):
	@scripts/install-git-hooks
	@echo

# rv32emu, fetched on demand ------------------------------------------------

.PHONY: deps toolchain emu
deps: emu
emu: $(EMU)

toolchain:
	@scripts/toolchain.sh $(CROSS_COMPILE)

# Target (RV32I, bare metal) build ------------------------------------------

TARGET_CFLAGS := $(RV_ARCH) -g -ffreestanding -fno-pic -fno-stack-protector \
                 -Wall -Wextra -Werror -Wvla -Icommon -Itests
TARGET_ASFLAGS := $(RV_ARCH) -g
TARGET_LDFLAGS := -T common/linker.ld --no-warn-rwx-segments

# The harness is built once at -O2 and shared, so it never skews a comparison.
COMMON_OBJS := $(addprefix $(BUILD)/common/, start.o perf.o rt.o print.o utest.o trt.o tutest.o)

$(BUILD)/common/%.o: common/%.S
	$(VECHO) "  AS\t$@\n"
	$(Q)mkdir -p $(@D)
	$(Q)$(RV_AS) $(TARGET_ASFLAGS) $< -o $@

$(BUILD)/common/%.o: tests/%.c
	$(VECHO) "  CC\t$@\n"
	$(Q)mkdir -p $(@D)
	$(Q)$(RV_CC) $(TARGET_CFLAGS) -O2 -c $< -o $@

$(BUILD)/common/%.o: common/%.c
	$(VECHO) "  CC\t$@\n"
	$(Q)mkdir -p $(@D)
	$(Q)$(RV_CC) $(TARGET_CFLAGS) -O2 -c $< -o $@

# Per program and optimization level:
#   build/<p>/<o>/impl.o   the C implementation, compiled with -<o>
#   build/<p>/impl_asm.o   the hand-written assembly, shared by every level
#   build/<p>/<o>/<p>.elf  both implementations plus tests and benchmark
# A blank line between generated rule sets keeps their recipes apart.
define NL


endef

define PROG_RULES
$(BUILD)/$(1)/impl_asm.o: src/$(1)/$(1).S
	$$(VECHO) "  AS\t$$@\n"
	$$(Q)mkdir -p $$(@D)
	$$(Q)$$(RV_AS) $$(TARGET_ASFLAGS) $$< -o $$@

$(foreach o,$(OPTS),$(call OPT_RULES,$(1),$(o))$(NL))
endef

define OPT_RULES
$(BUILD)/$(1)/$(2)/impl.o: src/$(1)/$(1).c src/$(1)/$(1).h common/utest.h
	$$(VECHO) "  CC\t$$@\n"
	$$(Q)mkdir -p $$(@D)
	$$(Q)$$(RV_CC) $$(TARGET_CFLAGS) -$(2) -Isrc/$(1) -c $$< -o $$@

$(BUILD)/$(1)/$(2)/main.o: src/$(1)/main.c src/$(1)/$(1).h common/bench.h common/utest.h
	$$(VECHO) "  CC\t$$@\n"
	$$(Q)mkdir -p $$(@D)
	$$(Q)$$(RV_CC) $$(TARGET_CFLAGS) -$(2) -Isrc/$(1) -DOPT_NAME='"$(2)"' -c $$< -o $$@

$(BUILD)/$(1)/$(2)/test.o: tests/t$(1).c src/$(1)/$(1).h common/utest.h
	$$(VECHO) "  CC\t$$@\n"
	$$(Q)mkdir -p $$(@D)
	$$(Q)$$(RV_CC) $$(TARGET_CFLAGS) -$(2) -Isrc/$(1) -c $$< -o $$@

$(BUILD)/$(1)/$(2)/$(1).elf: $(BUILD)/$(1)/$(2)/impl.o $(BUILD)/$(1)/impl_asm.o \
        $(BUILD)/$(1)/$(2)/main.o $(BUILD)/$(1)/$(2)/test.o $$(COMMON_OBJS) common/linker.ld
	$$(VECHO) "  LD\t$$@\n"
	$$(Q)$$(RV_LD) $$(TARGET_LDFLAGS) -o $$@ $$(filter %.o,$$^)
endef

$(foreach p,$(PROGS),$(eval $(call PROG_RULES,$(p))$(NL)))

ELFS := $(foreach p,$(PROGS),$(foreach o,$(OPTS),$(BUILD)/$(p)/$(o)/$(p).elf))
.PHONY: elfs
elfs: $(ELFS)

# The negative ELF has a deliberately failing test: run_tests() must make
# main return nonzero. It is never part of a submission.
NEG_ELF := $(BUILD)/neg/neg.elf
$(BUILD)/neg/main.o: tests/neg.c common/utest.h
	$(VECHO) "  CC\t$@\n"
	$(Q)mkdir -p $(@D)
	$(Q)$(RV_CC) $(TARGET_CFLAGS) -O2 -c $< -o $@
$(NEG_ELF): $(BUILD)/neg/main.o $(COMMON_OBJS) common/linker.ld
	$(VECHO) "  LD\t$@\n"
	$(Q)$(RV_LD) $(TARGET_LDFLAGS) -o $@ $(filter %.o,$^)
.PHONY: neg
neg: $(NEG_ELF)

# Host-side tests under sanitizers --------------------------------------------
#
# The C implementations and their tests are portable C, so they also build on
# the host, where AddressSanitizer and UBSan watch them. The host runner exits
# zero when the tests ran; the exit status 86 is reserved for a sanitizer
# report. Whether the tests pass is judged on the target by `make test`.

HOST_CC ?= cc
SAN_FLAGS := -fsanitize=address,undefined -fno-sanitize-recover=all \
             -fno-omit-frame-pointer -fno-common
HOST_CFLAGS := -g -O1 -Wall -Wextra -Wvla -Icommon -Itests $(SAN_FLAGS)

define HOST_RULE
$(BUILD)/host/$(1): common/utest.h tests/host.c tests/tutest.c tests/tutil.h tests/$(1)vec.h tests/host.c tests/t$(1).c src/$(1)/$(1).c src/$(1)/$(1).h
	$$(VECHO) "  HOSTCC\t$$@\n"
	$$(Q)mkdir -p $$(@D)
	$$(Q)$$(HOST_CC) $$(HOST_CFLAGS) -Isrc/$(1) -DPROG_REGISTER=$(1)_register_tests \
	    -o $$@ tests/host.c tests/tutest.c tests/t$(1).c src/$(1)/$(1).c common/utest.c
endef
$(foreach p,$(PROGS),$(eval $(call HOST_RULE,$(p))))

.PHONY: sanitize
sanitize: $(foreach p,$(PROGS),$(BUILD)/host/$(p))
	$(Q)for p in $(PROGS); do \
	    printf "  SAN\t%s\n" $$p; \
	    ASAN_OPTIONS=exitcode=86 UBSAN_OPTIONS=exitcode=86:print_stacktrace=1 \
	        $(BUILD)/host/$$p >/dev/null; rc=$$?; \
	    if [ $$rc -ne 0 ]; then echo "sanitizer failure in $$p (exit $$rc)"; exit 1; fi; \
	done

# Grading, analysis and listings --------------------------------------------

.PHONY: test cycles dump size check indent indent-check
test: elfs neg dump
	$(Q)scripts/repo.sh
	scripts/driver.py

cycles: elfs
	scripts/cycles.py --emu $(EMU) --build $(BUILD) --out results

dump: elfs
	$(Q)for p in $(PROGS); do for o in $(OPTS); do \
	    $(RV_OBJDUMP) -d -M no-aliases $(BUILD)/$$p/$$o/$$p.elf > $(BUILD)/$$p/$$o/$$p.dis; \
	done; $(RV_OBJDUMP) -d -M no-aliases $(BUILD)/$$p/impl_asm.o > $(BUILD)/$$p/asm.dis; done
	@echo "listings: $(BUILD)/<prog>/<opt>/<prog>.dis and $(BUILD)/<prog>/asm.dis"

size: elfs
	$(Q)for p in $(PROGS); do for o in $(OPTS); do \
	    echo "== $$p -$$o (C implementation)"; $(RV_SIZE) -A $(BUILD)/$$p/$$o/impl.o; \
	done; echo "== $$p (assembly)"; $(RV_SIZE) -A $(BUILD)/$$p/impl_asm.o; done

CLANG_FORMAT ?= clang-format-20
FORMAT_FILES := $(shell git ls-files 'src/*.[ch]' 'common/*.[ch]' 'tests/*.[ch]' 2>/dev/null)
indent:
	$(CLANG_FORMAT) -i $(FORMAT_FILES)
indent-check:
	$(Q)CLANG_FORMAT=$(CLANG_FORMAT) .ci/format.sh

check: elfs neg
	scripts/driver.py --quick

clean:
	rm -rf $(BUILD)
	rm -f results/*.csv results/*.md results/*.png

distclean: clean
	rm -rf third_party .cmd_history .out

############################################################
#
# kmodule-stage.mk
#
# Helper for Accton x86-64 platform module builds that pull
# kernel-module sources from
# packages/platforms/accton/x86-64/common/modules/src/.
#
# Consumers set the same variables that make/kmodule.mk expects
# (KERNELS, KMODULES, VENDOR, BASENAME, ARCH) plus:
#
#   ACCTON_COMMON_KMOD_SRCS
#     Space-separated list of basenames (without extension) to
#     pull from common/modules/src/. For each name <n>:
#       - <n>.c is copied into the staged build tree
#       - <n>.h is copied if it exists
#
# The rule creates a staged directory containing the platform's
# own src/modules/ contents plus the selected shared sources, and
# forwards that directory to kmodbuild.sh. The platform's own
# src/modules/Makefile must already contain the corresponding
# `obj-m += <n>.o` line(s); see README.md next to this file.
#
############################################################

ifndef KERNELS
$(error $$KERNELS must be set)
endif
ifndef KMODULES
$(error $$KMODULES must be set (platform src/modules directory))
endif
ifndef ARCH
$(error $$ARCH must be set)
endif
ifndef VENDOR
$(error $$VENDOR must be set)
endif
ifndef BASENAME
$(error $$BASENAME must be set)
endif

ACCTON_COMMON_KMOD_DIR := $(ONL)/packages/platforms/accton/x86-64/common/modules/src
STAGE_DIR := $(CURDIR)/.stage

SUBDIR := "onl/$(VENDOR)/$(BASENAME)"

modules:
	rm -rf lib $(STAGE_DIR)
	mkdir -p $(STAGE_DIR)
	cp -R $(KMODULES)/. $(STAGE_DIR)/
	@set -e; for name in $(ACCTON_COMMON_KMOD_SRCS); do \
	    src="$(ACCTON_COMMON_KMOD_DIR)/$$name.c"; \
	    hdr="$(ACCTON_COMMON_KMOD_DIR)/$$name.h"; \
	    if [ ! -f "$$src" ]; then \
	        echo "kmodule-stage: missing shared source $$src" >&2; \
	        exit 1; \
	    fi; \
	    cp "$$src" $(STAGE_DIR)/; \
	    if [ -f "$$hdr" ]; then cp "$$hdr" $(STAGE_DIR)/; fi; \
	done
	ARCH=$(ARCH) $(ONL)/tools/scripts/kmodbuild.sh "$(KERNELS)" "$(STAGE_DIR)" "$(SUBDIR)" "$(KINCLUDES)"
	rm -rf $(STAGE_DIR)

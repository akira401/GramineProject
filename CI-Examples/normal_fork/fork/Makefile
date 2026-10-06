# Copyright (C) 2023 Gramine contributors
# SPDX-License-Identifier: BSD-3-Clause

CFLAGS = -Wall -Wextra

ifeq ($(DEBUG),1)
GRAMINE_LOG_LEVEL = debug
CFLAGS += -g
else
GRAMINE_LOG_LEVEL = error
CFLAGS += -O3
endif

.PHONY: all
all: fork fork.manifest
ifeq ($(SGX),1)
all: fork.manifest.sgx fork.sig
endif

fork: fork.o

fork.o: fork.c

fork.manifest: fork.manifest.template fork
	gramine-manifest \
		-Dlog_level=$(GRAMINE_LOG_LEVEL) \
		$< $@
	gramine-manifest-check $@

fork.sig fork.manifest.sgx &: fork.manifest
	gramine-sgx-sign \
		--manifest $< \
		--output $<.sgx
	gramine-manifest-check $<.sgx

ifeq ($(SGX),)
GRAMINE = gramine-direct
else
GRAMINE = gramine-sgx
endif

.PHONY: check
check: all
	$(GRAMINE) fork > OUTPUT
	@echo "[ Gramine execution completed ]"
	@cat OUTPUT

.PHONY: clean
clean:
	$(RM) *.sig *.manifest.sgx *.manifest fork.o fork OUTPUT

.PHONY: distclean
distclean: clean

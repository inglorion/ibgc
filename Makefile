# Itty-Bitty Garbage Collector
#
# Automatic memory management for small-memory computers
#
# Copyright (c) 2022 Robbert Haarman
#
# SPDX-License-Identifier: MIT

CFLAGS ?= -Wall -Os

TARGETS = ibgc_test

all : $(TARGETS)

check : ibgc_test ibgc_test.out.expected
	./ibgc_test | diff -u ibgc_test.out.expected -

clean :

distclean :
	-rm $(TARGETS)

ibgc_test : ibgc_test.c ibgc.c
	$(CC) -o ibgc_test $(CFLAGS) ibgc_test.c

.PHONY : all check clean distclean

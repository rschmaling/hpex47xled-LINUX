SHELL = /usr/bin/bash

# compiler and flags
CC = gcc
CXX = g++
# FLAGS = -Wall -O2 -fvariable-expansion-in-unroller -ftree-loop-ivcanon -funroll-loops -fexpensive-optimizations -fomit-frame-pointer
# FLAGS = -Wall -O2 -std=c99 -Werror
FLAGS = -Wall -Werror -O2 -std=gnu99
CFLAGS = $(FLAGS)
CXXFLAGS = $(CFLAGS)
LDFLAGS = -ludev -pthread -lm
DEPS = hpex47xled.h
RCPREFIX := /etc/systemd/system/
RCFILE := hpex47xled.service
# PREFIX is environment variable if not set, set default value here.

ifeq ($(PREFIX),)
	PREFIX := /usr/local
endif


# build libraries and options

all: clean hpex47xled.o updatemonitor.o hpex47xled

hpex47xled.o: hpex47xled.c 
	$(CC) $(CFLAGS) -o $@ -c $^

updatemonitor.o: updatemonitor.c
	$(CC) $(CFLAGS) -o $@ -c $^

hpex47xled: hpex47xled.o updatemonitor.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

.PHONY: clean

clean:
	rm -f *.o hpex47xled *.core

.PHONY: install

install: all
	test -f $(RCPREFIX)$(RCFILE) || install -m 644 $(RCPREFIX)$(RCFILE)
	install -s -m 700 hpex47xled $(PREFIX)/bin/

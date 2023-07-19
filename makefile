#
# Makefile for lsame
#

LIBS := ../baselib/baselib.a
DIRS := -I ../baselib
DEBUG    := -g -DDEBUG
#OPTIMIZE := -O3
#PROFILE  := -pg -a
WARNINGS :=  -Wall -Wextra -pedantic
STD := -std=c11 -D_DEFAULT_SOURCE

CFLAGS := $(STD) $(DEBUG) $(WARNINGS) $(OPTIMIZE) $(PROFILE) $(DIRS)
CC := gcc $(GDEFS)

all: lsame

lsame.o:   lsame.c comp.h

lsame:  lsame.o comp.o $(LIBS)
	    $(CC) $(CFLAGS) -o $@ $^

comp.o: comp.c comp.h

.PHONY: clean
clean:	  
	  rm *.[o] lsame


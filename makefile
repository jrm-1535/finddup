#
# Makefile for fdup and fmis
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

all: fdup fmis

fdup:  fdup.o comp.o $(LIBS) -lmagic
	    $(CC) $(CFLAGS) -o $@ $^

fmis:  fmis.o comp.o $(LIBS) -lmagic
	    $(CC) $(CFLAGS) -o $@ $^

fdup.o:   fdup.c comp.h

comp.o: comp.c comp.h

fmis.o:   fmis.c comp.h

.PHONY: clean
clean:
	  rm *.[o] fdup fmis

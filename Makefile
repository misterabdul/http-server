CFLAGS := ${CFLAGS} -std=c99 -pedantic -Wall -Wextra -D_POSIX_C_SOURCE=200809L -I./src
LDFLAGS := ${LDFLAGS} -lpthread

ifeq ($(OS),Windows_NT)
    PLATFORM = windows
else
    UNAME_S := $(shell uname -s)
    ifeq ($(UNAME_S),Linux)
        PLATFORM = linux
    else ifeq ($(UNAME_S),Darwin)
        PLATFORM = darwin
	else
        PLATFORM = unix
    endif
endif

SRCDIR = src/core
PLTDIR = src/platforms/${PLATFORM}
BINDIR = bin
OBJDIR = obj
WWWDIR = www

MAIN = http-server
SRCS = $(wildcard ${SRCDIR}/*.c)
PLTS = $(wildcard ${PLTDIR}/*.c)
SRCOBJ = $(patsubst ${SRCDIR}/%.c,${OBJDIR}/core-%.o,${SRCS})
PLTOBJ = $(patsubst ${PLTDIR}/%.c,${OBJDIR}/platform-${PLATFORM}-%.o,${PLTS})

.PHONY: all clean

all: main

main: ${BINDIR}/${MAIN} ${BINDIR}/${WWWDIR}

clean:
	${RM} ${OBJDIR}/*
	${RM} ${BINDIR}/${MAIN}

${BINDIR}/${MAIN}: ${SRCOBJ} ${PLTOBJ}
	${CC} -o $@ $^ ${LDFLAGS}

${BINDIR}/${WWWDIR}:
	cp -r ${WWWDIR} ${BINDIR}/.

${OBJDIR}/core-%.o: ${SRCDIR}/%.c
	${CC} -o $@ -c $< ${CFLAGS}

${OBJDIR}/platform-${PLATFORM}-%.o: ${PLTDIR}/%.c
	${CC} -o $@ -c $< ${CFLAGS}

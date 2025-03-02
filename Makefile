CFLAGS := ${CFLAGS} -std=c99 -pedantic -Wall -Wextra -D_POSIX_C_SOURCE=200809L
LDFLAGS := ${LDFLAGS} -lpthread

SRCDIR = src
BINDIR = bin
OBJDIR = obj
WWWDIR = www

MAIN = http-server
SRCS = $(wildcard ${SRCDIR}/*.c)
SRCOBJ = $(patsubst ${SRCDIR}/%.c,${OBJDIR}/src-%.o,${SRCS})

.PHONY: all clean

all: main

main: ${BINDIR}/${MAIN} ${BINDIR}/${WWWDIR}

clean:
	${RM} ${OBJDIR}/*
	${RM} ${BINDIR}/${MAIN}

${BINDIR}/${MAIN}: ${SRCOBJ}
	${CC} -o $@ $^ ${LDFLAGS}

${BINDIR}/${WWWDIR}:
	cp -r ${WWWDIR} ${BINDIR}/.

${OBJDIR}/src-%.o: ${SRCDIR}/%.c
	${CC} -o $@ -c $< ${CFLAGS}

EXECS=test
MPICC?=mpicc

all: ${EXECS}

test:test.c
	${MPICC} -Wall -O3 -o test test.c -lm

clean:
	rm -f *.o ${EXECS} *~ *core

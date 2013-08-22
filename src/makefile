FLAGS= -Wall -g -pthread 
CC = gcc
PROG = heat
OBJS = heat.o



# GENERIC
all:		${PROG}

clean:
		rm ${OBJS} ${PROG} 

${PROG}:	${OBJS}
		${CC} ${FLAGS} ${OBJS} -o $@

#############################

input.o:	heat.c

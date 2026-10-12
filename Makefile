CFLAGS = -O2 -Wall -Wextra -std=c11 -D_GNU_SOURCE
LDLIBS = -lrt

all: producer consumer stress
producer: producer.c common.h
consumer: consumer.c common.h
stress: stress.c
clean:
	rm -f producer consumer stress
	rm -rf results

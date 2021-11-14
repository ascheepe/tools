CFLAGS=	-std=c89 -O2 -pipe -Wall -Wextra -Werror -pedantic
CFLAGS+= -fstack-protector-strong -fPIE -D_FORTIFY_SOURCE=2
CFLAGS+= -D_XOPEN_SOURCE=600
#CFLAGS+= -Og -g -fsanitize=address,leak

COMMON_OBJS= utils.o array.o

all: shuffle fit

shuffle: $(COMMON_OBJS) shuffle.o
	$(CC) $(CFLAGS) -o shuffle $(COMMON_OBJS) shuffle.o -lmagic

fit: $(COMMON_OBJS) fit.o
	$(CC) $(CFLAGS) -o fit $(COMMON_OBJS) fit.o

array.o: array.h
utils.o: utils.h

clean:
	rm -f *.o shuffle fit

install: shuffle fit
	install shuffle $(HOME)/bin
	install fit $(HOME)/bin
	strip -s $(HOME)/bin/shuffle $(HOME)/bin/fit

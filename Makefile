CFLAGS=	-std=c89 -O2 -pipe -Wall -Wextra -Werror -pedantic
#CFLAGS+= -Og -g -fsanitize=address,leak -fstack-protector-strong
#CFLAGS+= -D_FORTIFY_SOURCE=2

COMMON_OBJS= utils.o vector.o

all: shuffle fit mvd

shuffle: $(COMMON_OBJS) shuffle.o
	$(CC) $(CFLAGS) -o shuffle $(COMMON_OBJS) shuffle.o -lmagic

fit: $(COMMON_OBJS) fit.o
	$(CC) $(CFLAGS) -o fit $(COMMON_OBJS) fit.o

mvd: mvd.o
	$(CC) $(CFLAGS) -o mvd mvd.o

vector.o: vector.h
utils.o: utils.h

clean:
	rm -f *.o shuffle fit mvd

install: shuffle fit mvd
	install -s shuffle fit mvd $(HOME)/bin

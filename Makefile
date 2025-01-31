CFLAGS=	-std=c89 -O2 -pipe -Wall -Wextra -Werror -pedantic
#CFLAGS+= -Og -g -fsanitize=address,leak -fstack-protector-strong
#CFLAGS+= -D_FORTIFY_SOURCE=2

COMMON_OBJS= utils.o vector.o

all: shuffle fit mvtodate

shuffle: $(COMMON_OBJS) shuffle.o
	$(CC) $(CFLAGS) -o shuffle $(COMMON_OBJS) shuffle.o -lmagic

fit: $(COMMON_OBJS) fit.o
	$(CC) $(CFLAGS) -o fit $(COMMON_OBJS) fit.o

mvtodate: mvtodate.o
	$(CC) $(CFLAGS) -o mvtodate mvtodate.o

vector.o: vector.h
utils.o: utils.h

clean:
	rm -f *.o shuffle fit mvtodate

install: shuffle fit mvtodate
	install -s shuffle fit mvtodate $(HOME)/bin

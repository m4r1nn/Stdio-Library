all:build

stdio.o: stdio.c
	gcc -c -g -Wall -fPIC stdio.c

build: stdio.o
	gcc -shared stdio.o -o libso_stdio.so

.PHONY: clean

clean:
	rm -f libso_stdio.so stdio.o
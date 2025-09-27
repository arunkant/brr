default: brr

brr.o: brr.c
	cc -c brr.c -o brr.o

brr: brr.o
	cc brr.o -o brr

clean:
	-rm -f brr.o
	-rm -f brr
run: brr
	./brr

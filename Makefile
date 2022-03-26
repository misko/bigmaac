all: bigmaac.so bigmaac_debug.so preload test_bigmaac test

bigmaac.so: bigmaac.c bigmaac.h
	gcc -shared -fPIC bigmaac.c -o bigmaac.so -ldl -Wall -O3

bigmaac_debug.so: bigmaac.c bigmaac.h
	gcc -shared -DDEBUG -fPIC bigmaac.c -o bigmaac_debug.so -ldl -Wall -g

preload: preload.c
	gcc -Wall preload.c -o preload 

test_bigmaac: test_bigmaac.c bigmaac.h
	gcc -Wall test_bigmaac.c -o test_bigmaac -g

test: test_bigmaac preload
	./test_bigmaac > output_without_bigmaac
	./preload ./bigmaac.so ./test_bigmaac > output_with_bigmaac


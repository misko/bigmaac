all: bigmaac.so bigmaac_debug.so bigmaac_signal.so bigmaac_debug_signal.so preload test_bigmaac test bigmaac_main bigmaac_main_debug

bigmaac_main: bigmaac.c bigmaac.h
	gcc -DMAIN bigmaac.c -o bigmaac_main -Wall -g -ldl -fopenmp

bigmaac_main_debug: bigmaac.c bigmaac.h
	gcc -DMAIN -DDEBUG bigmaac.c -o bigmaac_main_debug -Wall -g -ldl -fopenmp

bigmaac.so: bigmaac.c bigmaac.h
	gcc -shared -fPIC bigmaac.c -o bigmaac.so -ldl -Wall -O3

bigmaac_debug.so: bigmaac.c bigmaac.h
	gcc -shared -DDEBUG -fPIC bigmaac.c -o bigmaac_debug.so -ldl -Wall -g

bigmaac_signal.so: bigmaac.c bigmaac.h
	gcc -shared -fPIC bigmaac.c -o bigmaac_signal.so -ldl -Wall -O3 -DBIGMAAC_SIGNAL

bigmaac_debug_signal.so: bigmaac.c bigmaac.h
	gcc -shared -DDEBUG -fPIC bigmaac.c -o bigmaac_debug_signal.so -ldl -Wall -g -DBIGMAAC_SIGNAL

preload: preload.c
	gcc -Wall preload.c -o preload 

test_bigmaac: test_bigmaac.c bigmaac.h
	gcc -Wall test_bigmaac.c -o test_bigmaac -g

test: bigmaac.so test_bigmaac preload
	./test_bigmaac > output_without_bigmaac
	./preload ./bigmaac.so ./test_bigmaac > output_with_bigmaac


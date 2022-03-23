bigmaac.so: bigmaac.c
	gcc -shared -fPIC bigmaac.c -o bigmaac.so -ldl -Wall -O3

preload: preload.c
	gcc -Wall preload.c -o preload 

all: bigmaac.so preload

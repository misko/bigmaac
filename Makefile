bigmaac.so: bigmaac.c
	gcc -shared -fPIC bigmaac.c -o bigmaac.so -ldl -Wall

preload: preload.c
	gcc -Wall preload.c -o preload

all: bigmaac.so preload

bigmaac.so: bigmaac.c
	gcc -shared -fPIC bigmaac.c -o bigmaac.so -ldl -Wall

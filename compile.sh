gcc -shared -fPIC bigmalloc.c -o bigmalloc.dylib -ldl -Wall
#DYLD_INSERT_LIBRARIES=./bigmalloc.dylib DYLD_FORCE_FLAT_NAMESPACE=1 `which wget` google.com

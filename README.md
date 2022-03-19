# BigMaac ( Big Malloc Access And Calloc )

## because sometimes a happy meal is not big enough

BigMaac intercepts calls to memory management that would normally get mapped directly to the heap/RAM and instead memory allocated from an alternative source (SSD/Disk).

The specific calls BigMaac intercepts are, 

`malloc()`, `calloc()`, `realloc()` and `free()`

If any of these calls are managing memory smaller than `BIGMAAC_MIN_SIZE` (env variable) chunks [ also called small fries ], BigMaac passes them directly through to the original memory management system. However if a memory call exceeds this size, it is no longer a small fry but instead it is a bigmaac, and therefore instead of using RAM directly it uses the Disk backed RAM storage through the magic of `mmap()`. 

For example, lets say you are on a system that has no swap available and only 2GB RAM, but! you would like to work with matricies of size 5GB [ definitely not a small fry ]. BigMaac lets you do that!
When numpy/python/anything makes the request for 5GB of memory, that call is intercepted and diverted by BigMaac. It is diverted through mmap to Disk backed RAM. Which means that the OS will keep whatever part of the 5GB matrix in can in RAM, and page the rest from disk. If you happen to run the same code on a system with 10GB RAM, it ~should leave the entire contents in RAM, with hopefully not much slowdown.

To use BigMaac, 
`make`

And then , 
`LD_PRELOAD=./bigmaac.so your-executable with all the arguments`

For example,
`LD_PRELOAD=./bigmaac.so python test.py`

#define _GNU_SOURCE
#include <pthread.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <dlfcn.h>
#include <string.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <errno.h>
#include <string.h>


#define DEFAULT_MIN_SIZE (1024*1024*10) //10MB
#define DEFAULT_TEMPLATE "/tmp/bigmaax.XXXXXXXX"
#define DEFAULT_CHUNK_TEMPLATE "bigmaax_chunk.XXXXXXXX"
#define DEFAULT_CHUNK_LIST_LENGTH 1
#define DEFAULT_MAX_BIGMAAC (1024L*1024*1024*512) //512GB
#define DEFAULT_MAX_FRIES (1024L*1024*1024*512) //512GB

typedef struct Chunks {
   void* ptr;
   char* tmp_fn;
   size_t size; 
} Chunk;

void remove_chunk(Chunk c);

pthread_mutex_t lock;

static void* (*real_malloc)(size_t)=NULL;
static void* (*real_calloc)(size_t,size_t)=NULL;
static void* (*real_free)(size_t)=NULL;
static void* (*real_realloc)(void*, size_t)=NULL;

static char *dir_name=NULL;
static size_t min_size=DEFAULT_MIN_SIZE; //10MB?

Chunk * chunk_list=NULL;
size_t chunk_list_length=0;

void* base_fries=0x0;
void* base_bigmaac=0x0;
void* end_fries=0x0;
void* end_bigmaac=0x0;

size_t size_fries=DEFAULT_MAX_FRIES;
size_t size_bigmaac=DEFAULT_MAX_FRIES;
size_t size_total=0;
 
size_t used_fries=0;
size_t used_bigmaac=0;
size_t page_size = 0;	

static void bigmaac_init(void)
{
	fprintf(stderr,"Loaded Bigmaac!\n");
	real_malloc = dlsym(RTLD_NEXT, "malloc");
	real_free = dlsym(RTLD_NEXT, "free");
	real_calloc = dlsym(RTLD_NEXT, "calloc");
	real_realloc = dlsym(RTLD_NEXT, "realloc");
	if (!real_malloc || !real_free || !real_calloc || !real_realloc) {
		fprintf(stderr, "Error in `dlsym`: %s\n", dlerror());
	}

    page_size = sysconf(_SC_PAGE_SIZE);	
	//load enviornment variables
	const char * template=getenv("BIGMAAC_TEMPLATE");
	if (template==NULL) {
		template=DEFAULT_TEMPLATE;
	}

	const char * env_min_size=getenv("BIGMAAC_MIN_SIZE");
	if (env_min_size!=NULL) {
		sscanf(env_min_size, "%zu", &min_size);
	}

	const char * env_size_fries=getenv("SIZE_FRIES");
	if (env_size_fries!=NULL) {
		sscanf(env_size_fries, "%zu", &size_fries);
	}
	const char * env_size_bigmaac=getenv("SIZE_BIGMAAC");
	if (env_size_bigmaac!=NULL) {
		sscanf(env_size_bigmaac, "%zu", &size_bigmaac);
	}

    size_total=size_fries+size_bigmaac;
	base_fries = mmap(NULL, size_total, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
    if (base_fries==MAP_FAILED) {
        printf("Oh dear, something went wrong with read()! %s\n", strerror(errno));
        fprintf(stderr,"Bigmaac: Failed to allocate primary maps\n");
    }    
    end_fries=((char*)base_fries)+size_fries;

    base_bigmaac=end_fries;
    end_bigmaac=((char*)base_fries)+size_total;

	//initalize the chunk list
	chunk_list_length=DEFAULT_CHUNK_LIST_LENGTH;
	chunk_list=(Chunk*)real_malloc(sizeof(Chunk)*chunk_list_length);
	if (chunk_list==NULL) {
		fprintf(stderr,"BigMaac: Failed to allocate memory for chunk list\n");
	}

	//initialize mutex for chunk list
	if (pthread_mutex_init(&lock, NULL) != 0) {
		printf("\n mutex init has failed\n");
	}

	//mkdtemp mutates the actual string
    dir_name=(char*)real_malloc(strlen(template)+1);
	if (dir_name==NULL) {
		fprintf(stderr,"BigMaac: Failed to allocate memory for dirname\n");
	}
    strcpy(dir_name,template);

	char * ret=mkdtemp(dir_name);
	if(ret == NULL)
	{
		perror("mkdtemp failed: ");
	}

}

__attribute__((constructor)) void init(void) {
    bigmaac_init();
}
__attribute__((destructor))  void fini(void) {

    for (int i=0; i<chunk_list_length; i++) {
        if (chunk_list[i].ptr!=NULL) {
            remove_chunk(chunk_list[i]);
        }
    }

	if(rmdir(dir_name) == -1)
	{
		perror("rmdir failed: ");
		fflush(stdout);
	}

}

void add_chunk(Chunk c) {
	pthread_mutex_lock(&lock);
	for (int i=0; i<chunk_list_length; i++) {
		if (chunk_list[i].ptr==NULL) {
			chunk_list[i]=c;
			pthread_mutex_unlock(&lock);
			return;	
		}	
	}

	//need to extend the list
	Chunk* new_chunk_list=(Chunk*)real_calloc(sizeof(Chunk)*chunk_list_length*2,1);
	if (new_chunk_list==NULL) {
		fprintf(stderr,"Failed to allocate chunk list\n");
	}
	memcpy(new_chunk_list,chunk_list,sizeof(Chunk)*chunk_list_length);
    chunk_list=new_chunk_list;
	chunk_list[chunk_list_length]=c;
	chunk_list_length=chunk_list_length*2;
	
	pthread_mutex_unlock(&lock);
}


Chunk create_chunk(size_t size) {
    fprintf(stderr,"Creating a new Bigmaac... %ld\n",size);
	//figure out a filename
	const char * filename_template=DEFAULT_CHUNK_TEMPLATE;
	char * filename=(char*)malloc(sizeof(char)*(strlen(dir_name)+1+strlen(filename_template)+1));
	if (filename==NULL) {
		fprintf(stderr,"Bigmaac: failed to allocate memory in remove_chunk");
	}
	sprintf(filename,"%s/%s", dir_name,filename_template);
	void* ret=mktemp(filename);
	if (ret==NULL) {
		fprintf(stderr,"Bigmaac: Failed to make temp file\n");
	}
	//open the file
	int fd=open(filename, O_RDWR | O_CREAT);
    //fprintf(stderr,"Opening file %s %s\n",filename,dir_name);
	if (fd<0) {
		fprintf(stderr,"Bigmaac: Failed to create file for mmap\n");
	}
	
	//resize the file
	ftruncate(fd, size);

    //guess address
    void * ptr = ((char*)base_bigmaac+used_bigmaac);
    uintptr_t residual=(uintptr_t)ptr % page_size;
    if ( residual != 0) {
        ptr=(char*)ptr+(page_size-residual);
    }

	//void * ret_ptr = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	void * ret_ptr = mmap(ptr, size, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED, fd, 0);	
	if (ret_ptr==MAP_FAILED) {
		fprintf(stderr,"Bigmaac: Failed to mmmap\n");
	}
    //fprintf(stderr,"GUESS %p got %p\n",ret_ptr,ptr);
    used_bigmaac+=size;

	Chunk c = { ptr, filename, size};
	return c;
}

void remove_chunk(Chunk c) {
    if (c.ptr!=NULL) {
        munmap(c.ptr,c.size);
		int ret = remove(c.tmp_fn);
	    real_free((size_t)c.tmp_fn);
    }    
}

int remove_chunk_with_ptr(void * ptr) {
	pthread_mutex_lock(&lock);
	for (int i=0; i<chunk_list_length; i++) {
		if (chunk_list[i].ptr==ptr){
            remove_chunk(chunk_list[i]);
            chunk_list[i].ptr=NULL;
	        pthread_mutex_unlock(&lock);
            return 1;
		}
	}
	pthread_mutex_unlock(&lock);
    return 0;
}



void *malloc(size_t size)
{
	if(real_malloc==NULL) {
		bigmaac_init();
	}

	void *p = NULL;
	if (size>min_size) {
        Chunk c=create_chunk(size);
        add_chunk(c);
        //p=c.ptr;
	    p = real_malloc(size);
	} else {
	    p = real_malloc(size);
		//fprintf(stderr, "FRIES %p %ld\n", p,size);
    }
	return p;
}


void free(void* ptr) {
	if(real_malloc==NULL) {
		bigmaac_init();
	}

    if (ptr<base_fries || ptr>end_bigmaac) {
        real_free((size_t)ptr);
    }

    int chunks_removed=remove_chunk_with_ptr(ptr); //Check if this pointer is>> address space reserved fr mmap 
    if (chunks_removed>0) {
        return;
    }
}


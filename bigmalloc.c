#define _GNU_SOURCE
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <dlfcn.h>
#include <string.h>
#include <fcntl.h>
#include <sys/mman.h>

#define DEFAULT_MIN_SIZE 100 //(1024*1024*10) //10MB
#define DEFAULT_TEMPLATE "/tmp/bigmaax.XXXXXXXX"
#define DEFAULT_CHUNK_TEMPLATE "bigmaax_chunk.XXXXXXXX"
#define DEFAULT_CHUNK_LIST_LENGTH 1

pthread_mutex_t lock;

static void* (*real_malloc)(size_t)=NULL;
static void* (*real_calloc)(size_t)=NULL;
static void* (*real_free)(size_t)=NULL;
static void* (*real_realloc)(void*, size_t)=NULL;

static char *dir_name=NULL;
static size_t min_size=DEFAULT_MIN_SIZE; //10MB?

typedef struct Chunks {
   void* ptr;
   char* tmp_fn;
} Chunk;

Chunk * chunk_list=NULL;
size_t chunk_list_length=0;
 

__attribute__((constructor)) void init(void) {
	fprintf(stderr,"Loaded BIGMAAC!\n");

	//load enviornment variables
	const char * template=getenv("BIGMAAC_TEMPLATE");
	if (template==NULL) {
		template=DEFAULT_TEMPLATE;
	}
	const char * env_min_size=getenv("BIGMAAC_MIN_SIZE");
	if (env_min_size!=NULL) {
		sscanf(env_min_size, "%zu", &min_size);
	}

	//initalize the chunk list
	chunk_list_length=DEFAULT_CHUNK_LIST_LENGTH;
	chunk_list=(Chunk*)malloc(sizeof(Chunk)*chunk_list_length);
	if (chunk_list==NULL) {
		fprintf(stderr,"BigMaac: Failed to allocate memory for chunk list\n");
	}

	//initialize mutex for chunk list
	if (pthread_mutex_init(&lock, NULL) != 0) {
		printf("\n mutex init has failed\n");
	}

	//mkdtemp mutates the actual string
	dir_name=strdup(template);

	char * ret=mkdtemp(dir_name);
	if(ret == NULL)
	{
		perror("mkdtemp failed: ");
	}

	fprintf(stderr,"MAKE DIR %s\n", dir_name);
}
__attribute__((destructor))  void fini(void) {
	fprintf(stderr,"RM DIR %s\n", dir_name);

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
	Chunk* new_chunk_list=(Chunk*)malloc(sizeof(Chunk)*chunk_list_length*2);
	if (new_chunk_list==NULL) {
		fprintf(stderr,"Failed to allocate chunk list\n");
	}
	memcpy(new_chunk_list,chunk_list,sizeof(Chunk)*chunk_list_length);
	chunk_list[chunk_list_length]=c;
	chunk_list_length=chunk_list_length*2;
	
	pthread_mutex_unlock(&lock);
}


Chunk create_chunk(size_t size) {
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
	if (fd<0) {
		fprintf(stderr,"Bigmaac: Failed to create file for mmap\n");
	}
	
	//resize the file
	ftruncate(fd, size);
	void * ptr = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);	
	if (ptr==MAP_FAILED) {
		fprintf(stderr,"Bigmaac: Failed to mmmap\n");
	}

	Chunk c = { ptr, filename};
	return c;
}

void remove_chunk(void * ptr) {
	pthread_mutex_lock(&lock);
	for (int i=0; i<chunk_list_length; i++) {
		if (chunk_list[i].ptr==ptr){
			chunk_list[i].ptr=NULL;
			int ret = remove(chunk_list[i].tmp_fn);
			free(chunk_list[i].tmp_fn);
		}
	}
	pthread_mutex_unlock(&lock);
}


static void bigmaac_init(void)
{
	real_malloc = dlsym(RTLD_NEXT, "malloc");
	real_free = dlsym(RTLD_NEXT, "free");
	real_calloc = dlsym(RTLD_NEXT, "calloc");
	real_realloc = dlsym(RTLD_NEXT, "realloc");
	if (!real_malloc || !real_free || !real_calloc || !real_realloc) {
		fprintf(stderr, "Error in `dlsym`: %s\n", dlerror());
	}
}

void *malloc(size_t size)
{
	if(real_malloc==NULL) {
		bigmaac_init();
	}

	void *p = NULL;
	p = real_malloc(size);
	fprintf(stderr,"%p %ld\n", p,size);
	if (size>min_size) {
		fprintf(stderr, "%p %ld\n", p,size);
	}
	return p;
}





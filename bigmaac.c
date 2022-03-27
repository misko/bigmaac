#define _GNU_SOURCE
#include <pthread.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <errno.h>
#include <string.h>

#include "bigmaac.h"

typedef struct Chunks {
    char* ptr;
    size_t size; 
} Chunk;

typedef struct heap {
    size_t used; 
    size_t length; 
    struct node ** node_array;
} heap;

typedef struct node {
    struct node * next;
    struct node * previous;
    int in_use;
    int heap_idx;
    char * ptr;
    size_t size;
    heap * heap;
} node;

static node * _head_bigmaacs; // head of the heap
static node * _head_fries; // head of the heap

#define IN_USE 0
#define FREE 1

//debug functions
#ifdef DEBUG
static void print_ll(node * head);
static void print_heap(heap* heap);
#endif
static void verify_memory(node * head,int global);

//inlineable functions
static inline int larger_gap(heap * heap, int idx_a, int idx_b);
static inline size_t size_to_page_multiple(size_t size,size_t page);

//heap operations
static heap * heap_new(size_t length);
static void heap_remove_idx(heap * heap, int idx);
static void heapify_up(heap * heap, int idx);
static void heapify_down(heap * heap, int idx);
static void heap_insert(node * head, node * n);
static void heap_free_node(node * head, node * n);
static node * heap_pop_split(node* head, size_t size);
static node * heap_find_node(node* head , void * ptr);
static void heapify_down(heap * heap, int idx);

//linked list operations
static node * ll_new(void* ptr, size_t size);

static void bigmaac_init(void);

//BigMaac helper functions
static void mmap_tmpfile(void * ptr, size_t size);
static int remove_chunk_with_ptr(node * head , void * ptr, Chunk * c);
static Chunk create_chunk(node* head, size_t size);

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

static void* (*real_malloc)(size_t)=NULL;
static void* (*real_calloc)(size_t,size_t)=NULL;
static void* (*real_free)(size_t)=NULL;
static void* (*real_realloc)(void*, size_t)=NULL;
static void* (*real_reallocarray)(void*,size_t,size_t)=NULL;

static size_t min_size_bigmaac=DEFAULT_MIN_BIGMAAC_SIZE; 
static size_t min_size_fry=DEFAULT_MIN_FRY_SIZE; 

static void* base_fries=0x0;
static void* base_bigmaac=0x0;
static void* end_fries=0x0;
static void* end_bigmaac=0x0;

static size_t size_fries=DEFAULT_MAX_FRIES;
static size_t size_bigmaac=DEFAULT_MAX_FRIES;

static size_t used_fries=0;
static size_t used_bigmaacs=0;
static size_t page_size = 0;	

static int load_state=0;

// DEBUG 
static inline void verify_memory(node * head, int global) {
#ifdef DEBUG
    size_t heap_free=0;
    for (int i =0; i<head->heap->used; i++) {
        assert(head->heap->node_array[i]->ptr!=NULL);
        heap_free+=head->heap->node_array[i]->size;
    }
    size_t t=0;
    size_t ll_free=0;
    node * prev=NULL;
    node *c =head;
    while(c!=NULL) {
        if (c->in_use==FREE) {
            ll_free+=c->size;
        }
        t+=c->size;
        assert(c->previous==prev);
        prev=c;
        c=c->next;
    }
    if (global==1) {
        if (head==_head_fries) {
            assert(used_fries==t-ll_free);
        } else {
            assert(used_bigmaacs==t-ll_free);
        }
    }
    assert(heap_free==ll_free);
    assert(t==size_bigmaac);
#endif
}

#ifdef DEBUG
static void print_ll(node * head) {
    while (head!=NULL) {
        fprintf(stderr,"%p n=%p, u=%d, p=%p, size=%ld, length=\n",head,head->next,head->in_use,head->previous,head->size);
        head=head->next;
    }
}

static void print_heap(heap* heap) {
    for (int i =0; i<heap->used; i++) {
        fprintf(stderr,"parent %d node %d , ptr=%p size=%ld\n",
                (i-1)/2, i,
                heap->node_array[i]->ptr,
                heap->node_array[i]->size);
    }
}
#endif

//Inlineables 

static int larger_gap(heap * heap, int idx_a, int idx_b) {
    return heap->node_array[idx_a]->size>heap->node_array[idx_b]->size ? idx_a : idx_b;
}

static inline size_t size_to_page_multiple(size_t size,size_t page) {
    size_t old_size=size;

    size_t residual=size % page;
    if ( residual != 0) {
        size=size+(page-residual);
    }

    assert(old_size<=size);

    return size;
}

// BigMaac heap

static heap * heap_new(size_t length) {
    heap * ha = (heap*)real_malloc(sizeof(heap));
    if (ha==NULL) {
        fprintf(stderr,"BigMalloc heap failed\n");
        assert(ha!=NULL);
    }     
    ha->node_array=(node**)real_malloc(sizeof(node*)*length);
    if (ha->node_array==NULL) {
        fprintf(stderr,"BigMalloc heap failed 2\n");
        assert(ha->node_array!=NULL);
    }
    ha->length=length;
    ha->used=0;
    return ha;
}


static void heap_remove_idx(heap * heap, int idx) {
    if (heap->used==1) {
        heap->used=0;
        heap->node_array[0]->heap_idx=-1;
        return;
    } 

    //take the last one and place it here
    heap->node_array[idx]->heap_idx=-1; // node is out of the heap
    heap->node_array[heap->used-1]->heap_idx=idx; //node has moved up in the heap
    heap->node_array[idx]=heap->node_array[heap->used-1];
    heap->used--; //the heap is now smaller

    heapify_down(heap,idx);
}

static void heapify_up(heap * heap, int idx) {
    if (idx==0) {
        return;
    }

    int parent_idx = (idx-1)/2;

    if (larger_gap(heap,idx,parent_idx)!=parent_idx) {
        node ** node_array = heap->node_array;
        //swap with the parent and keep going
        node_array[idx]->heap_idx=parent_idx;
        node_array[parent_idx]->heap_idx=idx;
        //now actuall swap them
        node * tmp = node_array[idx];
        node_array[idx]=node_array[parent_idx];
        node_array[parent_idx]=tmp;
        heapify_up(heap,parent_idx);
    }
}

static void heapify_down(heap * heap, int idx) {
    int largest_idx=idx;

    int left_child_idx = (idx+1)*2-1;
    int right_child_idx = (idx+1)*2;

    if (left_child_idx<heap->used) {
        largest_idx=larger_gap(heap,largest_idx,left_child_idx);
    }
    if (right_child_idx<heap->used) {
        largest_idx=larger_gap(heap,largest_idx,right_child_idx);
    }
    if (largest_idx!=idx) {
        node ** node_array = heap->node_array;
        //swap idx with largest_idx
        //first swap heap_idxs
        node_array[idx]->heap_idx=largest_idx;
        node_array[largest_idx]->heap_idx=idx;
        //now switch places
        node * tmp = node_array[idx];
        node_array[idx]=node_array[largest_idx];
        node_array[largest_idx]=tmp;

        heapify_down(heap,largest_idx);
    } // else we are done
}


static void heap_insert(node * head, node * n) {
    heap * heap = head->heap;
    if (heap->used==heap->length) {
        heap->node_array=(node**)real_realloc(heap->node_array,sizeof(node*)*heap->length*2);
        if (heap->node_array==NULL) {
            fprintf(stderr,"BigMaac : failed to heap insert\n"); 
        }
        head->heap->length*=2;
    }
    //gauranteed to have space
    heap->node_array[heap->used]=n;
    n->heap_idx=heap->used;

    heap->used++;

    heapify_up(heap, n->heap_idx);
}

static void heap_free_node(node * head, node * n) {
    assert(n->in_use==IN_USE);
    if (n->next!=NULL && n->next->in_use==FREE) {
        if (n->previous!=NULL && n->previous->in_use==FREE) {
            //merge previous into current
            node * tmp = n->previous; //node to remove

            //update size and pointer
            n->size+=tmp->size;
            n->ptr=tmp->ptr;

            //unlink the node tmp
            tmp->previous->next=n;
            n->previous=tmp->previous;

            heap_remove_idx(head->heap,tmp->heap_idx);
            real_free((size_t)tmp);
        }
        //add it to the next node
        n->next->size+=n->size;
        //update the pointer to this..
        n->next->ptr=n->ptr;
        //unlink this node from ll
        n->next->previous=n->previous;
        n->previous->next=n->next;
        //TODO free this node?
        heapify_up(head->heap, n->next->heap_idx);
        real_free((size_t)n);
    } else if (n->previous!=NULL && n->previous->in_use==FREE) {
        //add it to the previous node
        n->previous->size+=n->size;
        //unlnk this node from ll
        n->next->previous=n->previous;
        n->previous->next=n->next;

        heapify_up(head->heap, n->previous->heap_idx);
        real_free((size_t)n);
    } else { //add a whole new node
        n->in_use=FREE;
        heap_insert(head,n); 
    }
}

static node * heap_pop_split(node* head, size_t size) {
    verify_memory(head,0);
    if (head->heap->used==0) {
        fprintf(stderr,"There is no free memory!\n");
        assert(head->heap->used>9);
        //TODO resort to malloc?
    }

    heap * heap = head->heap;
    node ** node_array = heap->node_array;

    node * free_node = node_array[0];
    if (free_node->size<size) {
        fprintf(stderr,"BigMalloc heap failed to find a gap of size %ld , biggest gap is %ld, difference is %ld\n",size,free_node->size, size-free_node->size);
        assert(free_node->size>=size);
        //Try reserved space?
    }

    //check left and right child ( avoid further fragmenting largest chunk )
    //How can you have any pudding if you dont eat yer meat?
    int left_child_idx=1;
    if (heap->used>left_child_idx 
            && node_array[left_child_idx]->size>=size) {
        free_node=node_array[left_child_idx];
    }
    int right_child_idx=2;
    if (heap->used>right_child_idx 
            && node_array[right_child_idx]->size>=size 
            && node_array[right_child_idx]->size<free_node->size) {
        free_node=node_array[right_child_idx];
    }

    if (free_node->size==size) {
        heap_remove_idx(heap, free_node->heap_idx);
        free_node->in_use=IN_USE;
        verify_memory(head,1);
        return free_node;
    }

    //need to split this node
    node * used_node = (node*)real_malloc(sizeof(node));
    if (used_node==NULL) {
        fprintf(stderr,"BigMalloc failed to alloc new node\n");
        assert(used_node!=NULL);
    }
    //heapify from this node down
    *used_node = (node){
        .size = size,
            .ptr = free_node->ptr,
            .next = free_node,
            .previous = free_node->previous,
            .in_use = IN_USE,
            .heap_idx = -1
    };

    free_node->size-=size; // need to now heapify this node
    free_node->ptr=free_node->ptr+size;

    free_node->previous->next=used_node;
    free_node->previous=used_node;

    heapify_down(heap,free_node->heap_idx);
    verify_memory(head,1);

    return used_node;
}

static node * heap_find_node(node* head , void * ptr) {
    verify_memory(head,0);
    while (head!=NULL) {
        if (head->ptr==ptr) {
            return head;
        }
        head=head->next;
    }
    return NULL;
}

// BigMaac linked list

static node * ll_new(void* ptr, size_t size) {
    node * head = (node*)real_malloc(sizeof(node)*2);
    if (head==NULL) {
        fprintf(stderr,"BigMalloc heap: failed to make list\n");
        assert(head!=NULL);
    }

    node * e = head+1;
    *e = (node){
        .size = size,
            .ptr = ptr,
            .next = NULL,
            .previous = head,
            .in_use = FREE,
            .heap_idx = 0
    };

    *head = (node){
        .size = 0,
            .ptr = NULL,
            .next = e,
            .previous = NULL,
            .in_use = IN_USE,
            .heap_idx = -1
    };

    head->heap = heap_new(1);
    head->heap->node_array[0]=e;
    head->heap->used=1;

    return head;
}  

//BigMaac 

static void bigmaac_init(void)
{
    pthread_mutex_lock(&lock);

    if (load_state!=0) {
        pthread_mutex_unlock(&lock);
        fprintf(stderr,"Already init %d\n",load_state);
        return;
    }
#ifdef NOHEAP
    fprintf(stderr,"Loading Bigmaac NoHeap!\n");
#else
    fprintf(stderr,"Loading Bigmaac Heap!\n");
#endif
    load_state=1;
    real_malloc = dlsym(RTLD_NEXT, "malloc");
    real_free = dlsym(RTLD_NEXT, "free");
    real_calloc = dlsym(RTLD_NEXT, "calloc");
    real_realloc = dlsym(RTLD_NEXT, "realloc");
    real_reallocarray = dlsym(RTLD_NEXT, "reallocarray");
    if (!real_malloc || !real_free || !real_calloc || !real_realloc || !real_reallocarray) {
        fprintf(stderr, "Error in `dlsym`: %s\n", dlerror());
    }
    load_state=2;

    page_size = sysconf(_SC_PAGE_SIZE);	

    //load enviornment variables
    const char * template=getenv("BIGMAAC_TEMPLATE");
    if (template==NULL) {
        template=DEFAULT_TEMPLATE;
    }

    const char * env_min_size_bigmaac=getenv("BIGMAAC_MIN_BIGMAAC_SIZE");
    if (env_min_size_bigmaac!=NULL) {
        sscanf(env_min_size_bigmaac, "%zu", &min_size_bigmaac);
    }

    const char * env_min_size_fry=getenv("BIGMAAC_MIN_FRY_SIZE");
    if (env_min_size_fry!=NULL) {
        sscanf(env_min_size_fry, "%zu", &min_size_fry);
    } 
    if (min_size_fry==0) {
        min_size_fry=min_size_bigmaac; //disabled
    }

    const char * env_size_fries=getenv("SIZE_FRIES");
    if (env_size_fries!=NULL) {
        sscanf(env_size_fries, "%zu", &size_fries);
    }
    const char * env_size_bigmaac=getenv("SIZE_BIGMAAC");
    if (env_size_bigmaac!=NULL) {
        sscanf(env_size_bigmaac, "%zu", &size_bigmaac);
    }

    size_t size_total=size_fries+size_bigmaac;
    base_fries = mmap(NULL, size_total, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
    if (base_fries==MAP_FAILED) {
        printf("Oh dear, something went wrong with mmap()! %s\n", strerror(errno));
        assert(base_fries!=MAP_FAILED);
    }    
    end_fries=((char*)base_fries)+size_fries;

    base_bigmaac=end_fries;
    end_bigmaac=((char*)base_fries)+size_total;

    //initialize a heap
    _head_bigmaacs = ll_new(base_bigmaac,size_bigmaac);  
    _head_fries = ll_new(base_fries,size_fries);   
    assert(_head_fries!=_head_bigmaacs);

    load_state=3;
    pthread_mutex_unlock(&lock);
}


// BigMaac helper functions 

static void mmap_tmpfile(void * ptr, size_t size) {
    char * filename=(char*)real_malloc(sizeof(char)*(strlen(DEFAULT_TEMPLATE)+1));
    if (filename==NULL) {
        fprintf(stderr,"Bigmaac: failed to allocate memory in remove_chunk\n");
        assert(filename!=NULL);
    }
    strcpy(filename,DEFAULT_TEMPLATE);

    int fd=mkstemp(filename);
    if (fd<0) {
        fprintf(stderr,"Bigmaac: Failed to make temp file\n");
        assert(fd>=0);
    }

    unlink(filename);
    real_free((size_t)filename);

    //resize the file
    ftruncate(fd, size);

    void * ret_ptr = mmap(ptr, size, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED, fd, 0);	
    if (ret_ptr==MAP_FAILED) {
        fprintf(stderr,"Bigmaac: Failed to mmmap\n");
        assert(ret_ptr!=MAP_FAILED);
    }

    close(fd);//mmap keeps the fd open now
}

static Chunk create_chunk(node* head, size_t size) {
    pthread_mutex_lock(&lock);
    //page align the size requested
    if (head==_head_bigmaacs) {
        size=size_to_page_multiple(size,page_size);
        used_bigmaacs+=size;
    } else {
        size=size_to_page_multiple(size,128);
        used_fries+=size;
    }

    node * heap_chunk=heap_pop_split(head, size);
    pthread_mutex_unlock(&lock);

    if (heap_chunk==NULL) {
        fprintf(stderr,"BigMaac : Failed to find available space\n");
        assert(heap_chunk!=NULL);
    }

    if (head==_head_bigmaacs) {
        mmap_tmpfile(heap_chunk->ptr,size);
    }


    return (Chunk){ .ptr=heap_chunk->ptr, .size=size};
}

static int remove_chunk_with_ptr(node * head , void * ptr, Chunk * c) {
    pthread_mutex_lock(&lock);

    node * n = heap_find_node(head , ptr);
    if (n==NULL) {
        fprintf(stderr,"Cannot find node in BigMaac\n");
        pthread_mutex_unlock(&lock);
        return 0;
    }   

    if (c!=NULL) {
        size_t m = (n->size<c->size) ? n->size : c->size;
        memcpy(c->ptr,n->ptr,m);
    } 

    if (head==_head_bigmaacs) {
        void * remap = mmap(n->ptr, n->size, PROT_NONE, MAP_ANONYMOUS | MAP_FIXED | MAP_PRIVATE, -1, 0);	
        if (remap==NULL) {
            fprintf(stderr,"Oh dear, something went wrong with munmap()! %s\n", strerror(errno));
            return 0;
        }
        used_bigmaacs-=n->size;
    } else {
        used_fries-=n->size;
    }

    verify_memory(head,0);
    heap_free_node(head,n);
    verify_memory(head,1);
    pthread_mutex_unlock(&lock);

    return 1;
}


// BigMaac C library memory functions

void *malloc(size_t size)
{
    if(load_state==0 && real_malloc==NULL) {
        bigmaac_init();
    }

    if (load_state<3 || size==0) {
        return real_malloc(size);
    }

    void *p = NULL;
    if (size>min_size_bigmaac) {
        Chunk c=create_chunk(_head_bigmaacs,size);
        p=c.ptr;
    } else if (size>min_size_fry) {
        Chunk c=create_chunk(_head_fries,size);
        p=c.ptr;
    } else {
        p = real_malloc(size);
        if (p>=base_fries && p<end_bigmaac) {
            fprintf(stderr,"Malloc tried to hand out a BigMaac address p=%p s_offset=%ld e_offset=%ld\n",p,p-base_fries,end_bigmaac-p);
            assert(1==0);
        }
    }
    return p;
}

void *calloc(size_t count, size_t size)
{
    if (load_state>0 && load_state<3) {
        return NULL;
    }
    if(load_state==0 || real_malloc==NULL) {
        bigmaac_init();
    }

    if (load_state<3 || count==0 || size==0) {
        return real_calloc(count,size);
    }

    //library is loaded and count/size are reasonable
    void *p = NULL;
    if (size>min_size_bigmaac) {
        Chunk c=create_chunk(_head_bigmaacs,count*size);
        p=c.ptr;
    } else if (size>min_size_fry) {
        Chunk c=create_chunk(_head_fries,count*size);
        p=c.ptr;
        memset(p, 0, count*size);
    } else {
        p = real_calloc(count,size);
    }
    return p;
}

void *reallocarray(void * ptr, size_t size,size_t count) {
    return realloc(ptr,size*count);
}

void *realloc(void * ptr, size_t size)
{
    if(load_state==0 && real_malloc==NULL) {
        bigmaac_init();
    }

    //if library is not loaded use real_realloc
    if (load_state<3) {
        return real_realloc(ptr,size);
    }

    //if ptr is NULL then realloc is just malloc
    if (ptr==NULL) {
        return malloc(size);
    }

    //if size==0 then this is just a free call
    if (size==0) {
        free(ptr);
        return NULL;
    }

    //currently managed by BigMaac
    if (ptr>=base_fries && ptr<end_bigmaac) {
        //check if already allocated is big enough
        node * head = ptr<base_bigmaac ? _head_fries : _head_bigmaacs;

        pthread_mutex_lock(&lock);
        node * n = heap_find_node(head , ptr);
        if (n==NULL) {
            fprintf(stderr,"Cannot find node in BigMaac\n");
            assert(n!=NULL);
        }   
        pthread_mutex_unlock(&lock);
        if (n->size>=size) {
            return ptr;
        }

        //existing chunk is not big enough
        Chunk c;
        if (size>min_size_bigmaac) { //keep it managed here
            c=create_chunk(_head_bigmaacs,size);
            //c=(Chunk){ .ptr=real_malloc(size), .size=size };
        } else if (size>min_size_fry) {
            c=create_chunk(_head_fries,size);
            //c=(Chunk){ .ptr=real_malloc(size), .size=size };
        } else {
            c=(Chunk){ .ptr=real_malloc(size), .size=size };
        }

        int r=remove_chunk_with_ptr(head,ptr,&c); //Check if this pointer is>> address space reserved fr mmap
        if (r==0){ 
            fprintf(stderr,"BigMaac: failed to find part of memory\n");
            assert(r>0); //exit
        }
        return c.ptr;            
    }

    //currently managed by system
    void *p = NULL;
    if (size>min_size_fry || size>min_size_bigmaac) {
        //if (size>24476 && size<25476) { //debug pytest
        void* mallocd_p = real_realloc(ptr,size); //we have no idea of previous size
        if (mallocd_p==NULL) {
            fprintf(stderr,"BigMalloc: Failed to malloc\n");
            assert(1==0);
        }

        node * head = size>min_size_bigmaac ? _head_bigmaacs : _head_fries;
        Chunk c=create_chunk(head,size);
        p=c.ptr;

        memcpy(p,mallocd_p,size);

        real_free((size_t)mallocd_p);
    } else {
        p = real_realloc(ptr,size);
    }

    return p;
    }


    void free(void* ptr) {
        if(load_state==0 && real_malloc==NULL) {
            bigmaac_init();
        }

        //if ptr is managed by system or BigMaac is not loaded yet
        if (load_state<3 || ptr<base_fries || ptr>=end_bigmaac) {
            real_free((size_t)ptr);
            return;
        }

        //ptr is managed by BigMaac and library is fully loaded
        node * head = ptr<base_bigmaac ? _head_fries : _head_bigmaacs;
        int chunks_removed=remove_chunk_with_ptr(head,ptr,NULL); //Check if this pointer is>> address space reserved fr mmap 
        if (chunks_removed==0) {
            fprintf(stderr,"BigMaac: Free was called on pointer that was not alloc'd %p\n",ptr);
            assert(chunks_removed>0); //exit
        }
    }


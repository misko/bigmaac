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
    void* ptr;
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
    void * ptr;
    size_t size;
    heap * heap;
} node;


node * _head; // head of the heap

void remove_chunk(Chunk c);

pthread_mutex_t lock;

static void* (*real_malloc)(size_t)=NULL;
static void* (*real_calloc)(size_t,size_t)=NULL;
static void* (*real_free)(size_t)=NULL;
static void* (*real_realloc)(void*, size_t)=NULL;

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


static int load_state=0;


// bigmaac DS
//
#define IN_USE 0
#define FREE 1


void heapify_down(heap * heap, int idx);

heap * heap_new(size_t length) {
    heap * ha = (heap*)real_malloc(sizeof(heap));
    if (ha==NULL) {
        fprintf(stderr,"BigMalloc heap failed\n");
    }     
    ha->node_array=(node**)real_malloc(sizeof(node*)*length);
    if (ha->node_array==NULL) {
        fprintf(stderr,"BigMalloc heap failed 2\n");
    }
    ha->length=length;
    ha->used=0;
    return ha;
}

int larger_gap(heap * heap, int idx_a, int idx_b) {
    return heap->node_array[idx_a]->size>heap->node_array[idx_b]->size ? idx_a : idx_b;
}

void heap_remove_idx(heap * heap, int idx) {
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

void heapify_up(heap * heap, int idx) {
    if (idx==0) {
        return;
    }

    int parent_idx = (idx-1)/2;

    if (larger_gap(heap,idx,parent_idx)!=parent_idx) {
        //swap with the parent and keep going
        heap->node_array[idx]->heap_idx=parent_idx;
        heap->node_array[parent_idx]->heap_idx=idx;
        //now actuall swap them
        node * tmp = heap->node_array[idx];
        heap->node_array[idx]=heap->node_array[parent_idx];
        heap->node_array[parent_idx]=tmp;
        heapify_up(heap,parent_idx);
    }
}

void heapify_down(heap * heap, int idx) {
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
        //swap idx with largest_idx
        //first swap heap_idxs
        heap->node_array[idx]->heap_idx=largest_idx;
        heap->node_array[largest_idx]->heap_idx=idx;
        //now switch places
        node * tmp = heap->node_array[idx];
        heap->node_array[idx]=heap->node_array[largest_idx];
        heap->node_array[largest_idx]=tmp;

        heapify_down(heap,largest_idx);
    } // else we are done
}


void heap_insert(node * head, node * n) {
    //fprintf(stderr,"HEAP INSERT\n");
    //print_heap(head->heap);
    if (head->heap->used==head->heap->length) {
        head->heap->node_array=(node**)real_realloc(head->heap->node_array,sizeof(node*)*head->heap->length*2);
        if (head->heap->node_array==NULL) {
            fprintf(stderr,"BigMaac : failed to heap insert\n"); 
        }
        head->heap->length*=2;
    }
    //gauranteed to have space
    head->heap->node_array[head->heap->used]=n;
    n->heap_idx=head->heap->used;

    head->heap->used++;

    heapify_up(head->heap, n->heap_idx);
}

void print_heap(heap* heap) {
    //fprintf(stderr,"PRINT HEAP!\n");
    for (int i =0; i<heap->used; i++) {
        fprintf(stderr,"parent %d node %d , ptr=%p size=%ld\n",
            (i-1)/2, i,
            heap->node_array[i]->ptr,
            heap->node_array[i]->size);
    }
}

void heap_free_node(node * head, node * n) {
    //fprintf(stderr,"HEAP FREE\n");
    //print_heap(head->heap);
    if (n->next!=NULL && n->next->in_use==FREE &&
            n->previous!=NULL && n->previous->in_use==FREE) {
        //fprintf(stderr,"BigMaac: Double rainbow! %p %p\n",n->next->ptr,n->previous->ptr);
        //print_heap(head->heap);
        n->next->size+=n->size;
        n->next->size+=n->previous->size;

        n->next->previous=n->previous->previous;

        n->previous->previous->next=n->next;

        n->next->ptr=n->previous->ptr;

        heap_remove_idx(head->heap, n->previous->heap_idx);

        heapify_up(head->heap, n->next->heap_idx);
        free(n->previous);
        free(n);
        //print_heap(head->heap);
    } else if (n->next!=NULL && n->next->in_use==FREE) {
        //add it to the next node
        n->next->size+=n->size;
        //unlink this node from ll
        n->next->previous=n->previous;
        n->previous->next=n->next;
        //update the pointer to this..
        n->next->ptr=n->ptr;
        //TODO free this node?
        heapify_up(head->heap, n->next->heap_idx);
        free(n);
    } else if (n->previous!=NULL && n->previous->in_use==FREE) {
        //add it to the previous node
        n->previous->size+=n->size;
        //unlnk this node from ll
        n->next->previous=n->previous;
        n->previous->next=n->next;
        //TODO free this node?
        heapify_up(head->heap, n->previous->heap_idx);
        free(n);
    } else { //add a whole new node
        n->in_use=FREE;
        heap_insert(head,n); 
    }
}


node * heap_pop_split(node* head, size_t size) {
    //fprintf(stderr,"HEAP POP SPLIT\n");
    //print_heap(head->heap);
    if (head->heap->used==0) {
        fprintf(stderr,"There is no free memory!\n");
        //TODO resort to malloc?
    }

    node * free_node = head->heap->node_array[0];
    if (free_node->size<size) {
        fprintf(stderr,"BigMalloc heap failed to find a gap of size %ld , biggest gap is %ld, difference is %ld\n",size,free_node->size, size-free_node->size);
        //Try reserved space?
    }

    //check left and right child ( avoid further fragmenting largest chunk )
    //How can you have any pudding if you dont eat yer meat?
    int left_child_idx=1;
    if (head->heap->used>left_child_idx 
            && head->heap->node_array[left_child_idx]->size>=size) {
        free_node=head->heap->node_array[left_child_idx];
    }
    int right_child_idx=2;
    if (head->heap->used>right_child_idx 
            && head->heap->node_array[right_child_idx]->size>=size 
            && head->heap->node_array[right_child_idx]->size<free_node->size) {
        free_node=head->heap->node_array[right_child_idx];
    }

    if (free_node->size==size) {
        fprintf(stderr,"BigMalloc : what are the odds?\n");
        heap_remove_idx(head->heap, free_node->heap_idx);
        free_node->in_use=IN_USE;
        return free_node;
    }

    //need to split this node
    node * used_node = (node*)real_malloc(sizeof(node));
    if (used_node==NULL) {
        fprintf(stderr,"BigMalloc failed to alloc new node\n");
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

    heapify_down(head->heap,free_node->heap_idx);

    return used_node;

}

node * heap_find_node(node* head , void * ptr) {
    while (head!=NULL) {
        if (head->ptr==ptr) {
            return head;
        }
        head=head->next;
    }
    return NULL;
}

void print_ll(node * head) {
    while (head!=NULL) {
      fprintf(stderr,"%p n=%p, u=%d, p=%p, size=%ld, length=%\n",head,head->next,head->in_use,head->previous,head->size);
      head=head->next;
    }
}

node * ll_new(void* ptr, size_t size) {
    node * head = (node*)real_malloc(sizeof(node)*2);
    if (head==NULL) {
        fprintf(stderr,"BigMalloc heap: failed to make list\n");
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


//
//
// END bigmaac DS


size_t size_to_page_multiple(size_t size) {
    size_t residual=size % page_size;
    if ( residual != 0) {
        size=size+(page_size-residual);
    }
    return size;
}

void close_bigmaac() {
    pthread_mutex_lock(&lock);
    if (load_state>=0) {
        for (int i=0; i<chunk_list_length; i++) {
            if (chunk_list[i].ptr!=NULL) {
                remove_chunk(chunk_list[i]);
            }
        }

    }
    load_state=-1;
    pthread_mutex_unlock(&lock);
}


static void bigmaac_init(void)
{
    if (load_state!=0) {
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
    if (!real_malloc || !real_free || !real_calloc || !real_realloc) {
        fprintf(stderr, "Error in `dlsym`: %s\n", dlerror());
    }
    load_state=2;

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
        printf("Oh dear, something went wrong with mmap()! %s\n", strerror(errno));
    }    
    end_fries=((char*)base_fries)+size_fries;

    base_bigmaac=end_fries;
    end_bigmaac=((char*)base_fries)+size_total;

    //initialize a heap
    _head = ll_new(base_bigmaac,size_bigmaac);    

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

    load_state=3;
}


__attribute__((constructor)) void init(void) {
    //bigmaac_init();
}
__attribute__((destructor))  void fini(void) {
    close_bigmaac();

}


#ifdef NOHEAP

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
    //fprintf(stderr,"Creating a new Bigmaac... %ld\n",size);
    size=size_to_page_multiple(size);
    //figure out a filename
    char * filename=(char*)real_malloc(sizeof(char)*(strlen(DEFAULT_TEMPLATE)+1));
    if (filename==NULL) {
        fprintf(stderr,"Bigmaac: failed to allocate memory in remove_chunk\n");
    }
    strcpy(filename,DEFAULT_TEMPLATE);
    int fd=mkstemp(filename);
    if (fd<0) {
        fprintf(stderr,"Bigmaac: Failed to make temp file\n");
    }

    unlink(filename);

    //resize the file
    ftruncate(fd, size);

    //guess address
    void * ptr = ((char*)base_bigmaac+used_bigmaac);

    uintptr_t residual=(uintptr_t)ptr % page_size;
    if ( residual != 0) {
        ptr=(char*)ptr+(page_size-residual);
    }
    if (ptr+size>end_bigmaac) {
        fprintf(stderr,"BigMaac OUT OF BOUNDS!\n");
    }
    //void * ret_ptr = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    void * ret_ptr = mmap(ptr, size, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED, fd, 0);	
    if (ret_ptr==MAP_FAILED) {
        fprintf(stderr,"Bigmaac: Failed to mmmap\n");
    }
    close(fd);
    used_bigmaac=ret_ptr-base_bigmaac+size; //this is incorrect, pages dont align always!

    Chunk c = { ptr, size};
    add_chunk(c);
    return c;
}

void remove_chunk(Chunk c) {
    if (c.ptr!=NULL) {
        void * remap = mmap(c.ptr, c.size, PROT_NONE, MAP_ANONYMOUS | MAP_FIXED | MAP_PRIVATE, -1, 0);	
        if (remap==NULL) {
            printf("Oh dear, something went wrong with munmap()! %s\n", strerror(errno));
        }
    }    
}

int remove_chunk_with_ptr(void * ptr, Chunk * c) {
    pthread_mutex_lock(&lock);

    for (int i=0; i<chunk_list_length; i++) {
        if (chunk_list[i].ptr==ptr){
            if (c!=NULL) {
                size_t m = (chunk_list[i].size<c->size) ? chunk_list[i].size : c->size;
                memcpy(c->ptr,chunk_list[i].ptr,m);
            }
            remove_chunk(chunk_list[i]);
            chunk_list[i].ptr=NULL;
            pthread_mutex_unlock(&lock);

            return 1;
        }
    }
    pthread_mutex_unlock(&lock);
    return 0;
}

#else
Chunk create_chunk(size_t size) {
    //fprintf(stderr,"Creating a new Bigmaac... %ld\n",size);
    size=size_to_page_multiple(size);
    //figure out a filename
    char * filename=(char*)real_malloc(sizeof(char)*(strlen(DEFAULT_TEMPLATE)+1));
    if (filename==NULL) {
        fprintf(stderr,"Bigmaac: failed to allocate memory in remove_chunk\n");
    }
    strcpy(filename,DEFAULT_TEMPLATE);
    int fd=mkstemp(filename);
    if (fd<0) {
        fprintf(stderr,"Bigmaac: Failed to make temp file\n");
    }

    unlink(filename);
    free(filename);

    //resize the file
    ftruncate(fd, size);

    //guess address
    pthread_mutex_lock(&lock);
    node * heap_chunk=heap_pop_split(_head, size);
    //fprintf(stderr,"HEAP USED %ld LENGTH %ld\n",_head->heap->used,_head->heap->length);
    pthread_mutex_unlock(&lock);

    void * ret_ptr = mmap(heap_chunk->ptr, size, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED, fd, 0);	
    if (ret_ptr==MAP_FAILED) {
        fprintf(stderr,"Bigmaac: Failed to mmmap\n");
    }
    close(fd);

    return (Chunk){ heap_chunk->ptr, size};
}

int remove_chunk_with_ptr(void * ptr, Chunk * c) {
    pthread_mutex_lock(&lock);
    node * n = heap_find_node(_head , ptr);
    if (n==NULL) {
        fprintf(stderr,"Cannot find node in BigMaac\n");
    }   

    //if c is not null
    if (c!=NULL) {
        size_t m = (n->size<c->size) ? n->size : c->size;
        memcpy(c->ptr,n->ptr,m);
    } 

    void * remap = mmap(n->ptr, n->size, PROT_NONE, MAP_ANONYMOUS | MAP_FIXED | MAP_PRIVATE, -1, 0);	
    if (remap==NULL) {
        printf("Oh dear, something went wrong with munmap()! %s\n", strerror(errno));
    }

    heap_free_node(_head,n);

    pthread_mutex_unlock(&lock);

    return 1;
}


#endif

void *malloc(size_t size)
{
    if(load_state==0 && real_malloc==NULL) {
        bigmaac_init();
    }
    if (load_state<2) {
        return NULL;
    }

    void *p = NULL;
    if (load_state==3 && size>min_size) {
        Chunk c=create_chunk(size);
        p=c.ptr;
    } else {
        p = real_malloc(size);
        if (p>base_fries && p<end_bigmaac) {
            fprintf(stderr,"Malloc tried to hand out a BigMaac address p=%p s_offset=%ld e_offset=%ld\n",p,p-base_fries,end_bigmaac-p);
            assert(1==0);
        }
    }
    return p;
}

void *calloc(size_t count, size_t size)
{
    if(load_state==0 && real_malloc==NULL) {
        bigmaac_init();
    }
    if (load_state<2) {
        return NULL;
    }

    void *p = NULL;
    if (load_state==3 && size>min_size) {
        Chunk c=create_chunk(size);
        p=c.ptr;
    } else {
        p = real_calloc(count,size);
    }
    return p;
}

void *realloc(void * ptr, size_t size)
{
    if(load_state==0 && real_malloc==NULL) {
        bigmaac_init();
    }
    assert(size>0);
    if (load_state==3 && (ptr>=base_fries && ptr<end_bigmaac)) {
        Chunk c;
        if (size>min_size) { //keep it managed here
            c=create_chunk(size);
        } else {
            c=(Chunk){ malloc(size), size };
        }
        remove_chunk_with_ptr(ptr,&c); //Check if this pointer is>> address space reserved fr mmap
        return c.ptr;            
    }

    void *p = NULL;
    if (load_state==3 && size>min_size) {
        void* mallocd_p = real_realloc(ptr,size); //we have no idea of previous size

        Chunk c=create_chunk(size);
        p=c.ptr;

        memcpy(p,mallocd_p,size);
    } else {
        p = real_realloc(ptr,size);
    }
    return p;
}


void free(void* ptr) {
    if (load_state<0) {
        return;
    }
    if(load_state==0 && real_malloc==NULL) {
        bigmaac_init();
    }

    if (load_state!=3 || ptr<base_fries || ptr>end_bigmaac) {
        real_free((size_t)ptr);
        return;
    }
    int chunks_removed=remove_chunk_with_ptr(ptr,NULL); //Check if this pointer is>> address space reserved fr mmap 
    if (chunks_removed==0) {
        fprintf(stderr,"BigMaac: Free was called on pointer that was not alloc'd %p\n",ptr);
    }
}


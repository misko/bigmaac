#define _GNU_SOURCE         /* See feature_test_macros(7) */
#include <stdlib.h>
#include <stdio.h>

#define IN_USE 0
#define FREE 1

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

void heapify_down(heap * heap, int idx);

heap * heap_new(size_t length) {
    heap * ha = (heap*)malloc(sizeof(heap));
    if (ha==NULL) {
        fprintf(stderr,"BigMalloc heap failed\n");
    }     
    ha->node_array=(node**)malloc(sizeof(node*)*length);
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

    fprintf(stderr,"n %d %ld, p %d %ld, %d\n",
            idx,heap->node_array[idx]->size,
            parent_idx, heap->node_array[parent_idx]->size,
            larger_gap(heap,idx,parent_idx)); 
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
    if (head->heap->used==head->heap->length) {
        head->heap->node_array=(node**)realloc(head->heap->node_array,sizeof(node*)*head->heap->length*2);
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

void heap_free_node(node * head, node * n) {
    if (n->next!=NULL && n->next->in_use==FREE) {
        fprintf(stderr,"Merge free next\n");
        //add it to the next node
        n->next->size+=n->size;
        //unlink this node from ll
        n->next->previous=n->previous;
        n->previous->next=n->next;
        //update the pointer to this..
        n->next->ptr=n->ptr;
        //TODO free this node?
        free(n);
    } else if (n->previous!=NULL && n->previous->in_use==FREE) {
        fprintf(stderr,"Merge free prev\n");
        //add it to the previous node
        n->previous->size+=n->size;
        //unlnk this node from ll
        n->next->previous=n->previous;
        n->previous->next=n->next;
        //TODO free this node?
        free(n);
    } else { //add a whole new node
        fprintf(stderr,"Heap insert %ld\n",n->size);
        n->in_use=FREE;
        heap_insert(head,n); 
    }
}

node * heap_pop_split(node* head, size_t size) {
    node * largest_gap = head->heap->node_array[0];
    if (largest_gap->size<size) {
        fprintf(stderr,"BigMalloc heap failed to find a gap %d vs %d\n",largest_gap->size, size);
    }

    if (largest_gap->size==size) {
        heap_remove_idx(head->heap, largest_gap->heap_idx);
        largest_gap->in_use=IN_USE;
        return largest_gap;
    }

    //need to split this node
    node * used_node = (node*)malloc(sizeof(node));
    if (used_node==NULL) {
        fprintf(stderr,"BigMalloc failed to alloc new node\n");
    }
    //heapify from this node down
    used_node->size=size;
    largest_gap->size-=size; // need to now heapify this node
    largest_gap->ptr=largest_gap->ptr+size;

    used_node->previous=largest_gap->previous;
    used_node->next=largest_gap;

    largest_gap->previous->next=used_node;
    largest_gap->previous=used_node;

    used_node->in_use=IN_USE;
    used_node->heap_idx=-1; //not in the gap heap

    heapify_down(head->heap,largest_gap->heap_idx);

    return used_node;

}

void print_ll(node * head) {
    while (head!=NULL) {
      fprintf(stderr,"%p n=%p, u=%d, p=%p, size=%ld, length=%\n",head,head->next,head->in_use,head->previous,head->size);
      head=head->next;
    }
}

node * ll_new(void* ptr, size_t size) {
    node * head = (node*)malloc(sizeof(node)*2);
    if (head==NULL) {
        fprintf(stderr,"BigMalloc heap: failed to make list\n");
    }

    node * e = head+1;
    e->previous=head;
    e->next=NULL;
    e->size=size;
    e->in_use=FREE;
    e->heap_idx=0;
    e->ptr=ptr;

    head->next=e;
    head->previous=NULL;
    head->size=0;
    head->in_use=IN_USE;
    head->heap_idx=-1;
    head->ptr=NULL;

    head->heap = heap_new(1);
    head->heap->node_array[0]=e;
    head->heap->used=1;

    return head;
}  



int main() {
    node * head = ll_new(malloc(1024),1024);    
    node * r3=heap_pop_split(head, 10);
    fprintf(stderr,"Heap used %ld heap length %ld, r%p %ld\n",head->heap->used, head->heap->length, r3, r3->size);
    node * r4=heap_pop_split(head, 10);
    fprintf(stderr,"Heap used %ld heap length %ld, r%p %ld\n",head->heap->used, head->heap->length, r4, r4->size);
    node * r2=heap_pop_split(head, 100);
    fprintf(stderr,"Heap used %ld heap length %ld, r%p %ld\n",head->heap->used, head->heap->length, r2, r2->size);
    node * r=heap_pop_split(head, 900);
    fprintf(stderr,"Heap used %ld heap length %ld, r%p %ld\n",head->heap->used, head->heap->length, r, r->size);
    fprintf(stderr,"FREE\n");
    print_ll(head);
    heap_free_node(head,r4);

    fprintf(stderr,"FREE\n");
    print_ll(head);
    heap_free_node(head,r2);
    fprintf(stderr,"FREE\n");
    print_ll(head);
    heap_free_node(head,r3);
    r2=heap_pop_split(head, 90);
    fprintf(stderr,"Heap used %ld heap length %ld, r%p %ld\n",head->heap->used, head->heap->length, r2, r2->size);
    /*while (head!=NULL) {
      fprintf(stderr,"%p n=%p, p=%p, size=%ld, length=%\n",head,head->next,head->previous,head->size);
      head=head->next;
      }*/
}

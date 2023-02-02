#include <stdio.h>
#include <stdlib.h>
#include "bigmaac.h"

#define N 15

int seed=0xbeef;


int checksum(int * p, size_t n ) {
    int c = 0;
    for (int i=0; i<n; i++) {
        c+=p[i]*i;
    }
    return c;
}

int main() {
    int n_ints=100+DEFAULT_MIN_BIGMAAC_SIZE/sizeof(int);

    /*void * v1=(void*)malloc(sizeof(int)*n_ints);
    fprintf(stderr,"WTF\n");
    realloc(v1, 2*sizeof(int)*n_ints);
    return 0;
    //void * v1=(void*)malloc(sizeof(int)*n_ints);*/


    int * chunks[N];
    int checksums[N];
    int sizes[N];

    n_ints++; //make sure its big enough to trigger
    //lets test some mallocs
    fprintf(stderr,"Malloc\n");
    for (int i=0; i<N; i++) {
        seed+=i*i;
        int more=seed%(17*(1+(i > 0 ? i : -i)));
        //more=more > 0 ? more : -more;
        fprintf(stderr,"MORE IS %d\n",more);
        sizes[i]=more+n_ints;
        fprintf(stderr,"ALLOC %d\n",sizes[i]);
        chunks[i]=(int*)malloc(sizeof(int)*sizes[i]);
        if (chunks[i]==NULL) {
            fprintf(stderr,"Failed to malloc chunk %d\n",i);
            exit(1);
        }
        for (int j=0; j<sizes[i]; j++) {
            seed+=((seed%9)*(seed%7)+1)%(seed-1-seed%2+17);
            chunks[i][j]=seed;
        }
        checksums[i]=checksum(chunks[i],sizes[i]);
        fprintf(stdout,"%d %d\n",i,checksums[i]);
    }
    /*fprintf(stderr,"Malloc/Free\n");
    for (int i=0; i<N; i++) {
        seed+=((seed%9)*(seed%7)+1)%(seed-1-seed%2);
        if (seed%(i+1)>i/2) {
            free(chunks[i]);
            chunks[i]=NULL;
            if (seed%(i+1)<i/4) {
                sizes[i]+=(seed%100);
                chunks[i]=(int*)malloc(sizes[i]);
                if (chunks[i]==NULL) {
                    fprintf(stderr,"Failed to malloc chunk 2 %d\n",i);
                    exit(1);
                }
                for (int j=0; j<sizes[i]; j++) {
                    seed+=((seed%9)*(seed%7)+1)%(seed-1-seed%2);
                    chunks[i][j]=seed;
                }
                checksums[i]=checksum(chunks[i],sizes[i]);
                fprintf(stdout,"%d %d\n",i,checksums[i]);
            }
        }
    }
    fprintf(stderr,"Malloc/Free post\n");
    for (int i=0; i<N; i++) {
        if (chunks[i]!=NULL) {
            checksums[i]=checksum(chunks[i],sizes[i]);
            fprintf(stdout,"%d %d\n",i,checksums[i]);
        }
    }*/
    fprintf(stderr,"Realloc\n");
    for (int i=0; i<N; i++) {
        if (chunks[i]==NULL) {
            continue;
        }
        seed+=((seed%9)*(seed%7)+1)%(seed-1-seed%2);
        if (seed%(i+1)>i/2) {
            //free(chunks[i]);
            //chunks[i]=NULL;
        } else {
            int old_size=sizes[i];
            sizes[i]+=10000; //#(seed%9700001);
            /*if (seed%2==0) {
                seed+=((seed%9)*(seed%7)+1)%(seed-1-seed%2);
                sizes[i]-=(seed%101);
            }*/
            fprintf(stderr,"OLD SIZE %d new size %d\n",old_size,sizes[i]);
            chunks[i]=(int*)realloc(chunks[i],sizeof(int)*sizes[i]);
            chunks[i]=(int*)realloc(chunks[i],sizeof(int)*sizes[i]+100000);
            if (sizes[i]>old_size) {
                for (int j=old_size; j<sizes[i]; j++) {
                    seed+=((seed%9)*(seed%7)+1)%(seed-1-seed%2);
                    chunks[i][j]=seed;
                }
            }

        }
    }
    for (int i=0; i<N; i++) {
        if (chunks[i]!=NULL) {
            checksums[i]=checksum(chunks[i],sizes[i]);
            fprintf(stdout,"%d %d\n",i,checksums[i]);
        }
    }
}

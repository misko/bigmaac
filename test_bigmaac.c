#include <stdio.h>
#include <stdlib.h>
#include "bigmaac.h"

#define N 20

int seed=0xbeef;


int checksum(int * p, size_t n ) {
    int c = 0;
    for (int i=0; i<n; i++) {
        c+=p[i]*i;
    }
    return c;
}

int main() {
    int * chunks[N];
    int checksums[N];
    int sizes[N];

    int n_ints=DEFAULT_MIN_SIZE/sizeof(int);
    n_ints++; //make sure its big enough to trigger
    //lets test some mallocs
    for (int i=0; i<N; i++) {
        int more=seed%100;
        sizes[i]=more+n_ints;
        chunks[i]=(int*)malloc(sizeof(int)*sizes[i]);
        if (chunks[i]==NULL) {
            fprintf(stderr,"Failed to malloc chunk %d\n",i);
            exit(1);
        }
        for (int j=0; j<sizes[i]; j++) {
            seed+=((seed%9)*(seed%7)+1)%(seed-1-seed%2);
            chunks[i][j]=seed;
        }
        checksums[i]=checksum(chunks[i],sizes[i]);
        fprintf(stdout,"%d %d\n",i,checksums[i]);
    }
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
    for (int i=0; i<N; i++) {
        if (chunks[i]!=NULL) {
            checksums[i]=checksum(chunks[i],sizes[i]);
            fprintf(stdout,"%d %d\n",i,checksums[i]);
        }
    }
    for (int i=0; i<N; i++) {
        if (chunks[i]==NULL) {
            continue;
        }
        seed+=((seed%9)*(seed%7)+1)%(seed-1-seed%2);
        if (seed%(i+1)>i/2) {
            free(chunks[i]);
            chunks[i]=NULL;
        } else {
            int old_size=sizes[i];
            sizes[i]+=(seed%97);
            if (seed%2==0) {
                seed+=((seed%9)*(seed%7)+1)%(seed-1-seed%2);
                sizes[i]-=(seed%101);
            }
            chunks[i]=(int*)realloc(chunks[i],sizeof(int)*sizes[i]);
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

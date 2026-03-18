#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>
#include <stdint.h>
#include "mm.h"
#include "memlib.h"

team_t team = {
    "20221598",
    "SoYeon Lee",
    "yfresian@sogang.ac.kr",
};

// Constants and Macros
#define WSIZE 4                 // Word size (bytes)
#define DSIZE 8                 // Double word size (bytes)
#define CHUNKSIZE (1 << 7)     // Initial heap size
#define MIN_BLCOK_SIZE 16
#define MAX(x, y) ((x) > (y) ? (x) : (y))

/* Rounds up to the nearest multiple of DSIZE */
#define ALIGN(size) (((size) + (DSIZE-1)) & ~0x7)

#define SIZE_T_SIZE (ALIGN(sizeof(size_t)))

/* Pack a size and allocated bit into a word */
#define PACK(size, prev_alloc, alloc) ((size) |prev_alloc <<1|(alloc))

/* Read and write a word at address p */
#define GET(p) (*(unsigned int*)(p))
#define PUT(p, val) (*(unsigned int*)(p) = (val))

/* Read the size and allocated fields from address p */
#define GET_SIZE(p) (GET(p) & ~0x7)
#define GET_ALLOC(p) (GET(p) & 0x1)
#define GET_PREV_ALLOC(p) ((GET(p) & 0x2) >> 1)

/* Given block ptr bp, compute address of its header and footer */
#define HDRP(bp) ((char*)(bp) - WSIZE)
#define FTRP(bp) ((char*)(bp) + GET_SIZE(HDRP(bp)) - DSIZE)

/* Given block ptr bp, compute address of next and previous blocks */
#define NEXT_BLKP(bp) ((char *)(bp) + GET_SIZE(((char *)(bp) - WSIZE)))
#define PREV_BLKP(bp) ((char*)(bp) - GET_SIZE((char*)(bp) - DSIZE))

/* Free list pointer operations */
#define PREC_FREE(bp) (*(char**)(bp))
#define SUCC_FREE(bp) (*(char**)(bp + WSIZE))

/* Function prototypes */
static void *extend_heap(size_t words);
static void *find_fit(size_t asize);
static void *coalesce(void* bp);
static void place(void *bp, size_t asize);
static int find_idx(size_t size);
static void insert_block(void* bp);
static void remove_block(void *bp);

// length of segregated list
#define LISTLIMIT 16
#define GET_LIST(i)   (*(void **)(free_array + (i*WSIZE)))
#define SET_LIST(i, ptr) ((GET_LIST(i)) = ptr)
/* Global variables */
static void *heap_listp;
static void *free_array;

/* 
 * mm_init - Initialize the memory manager 
 */
int mm_init(void) {
    free_array = NULL;
    heap_listp = NULL;
    // We put our free_lists on the heap
    if ((free_array = mem_sbrk(LISTLIMIT*WSIZE)) == (void *)-1)
      return -1;

    /* Create the initial empty heap */
    if ((heap_listp = mem_sbrk(4*WSIZE)) == (void *)-1)
    return -1;

    // initialize free_lists
    int i;
    for(i = 0; i  < LISTLIMIT; i++)
        SET_LIST(i, NULL);
    
    /* Initialize the empty heap */
    PUT(heap_listp, 0);                       // Alignment padding
    PUT(heap_listp + (1 * WSIZE), PACK(DSIZE, 1, 1));  // Prologue header
    PUT(heap_listp + (2 * WSIZE), PACK(DSIZE, 1, 1));  // Prologue footer
    PUT(heap_listp + (3 * WSIZE), PACK(0, 1, 1));      // Epilogue header

    heap_listp += DSIZE;

    // Extend the empty heap with a free block of CHUNKSIZE bytes
    if (extend_heap(CHUNKSIZE / WSIZE) == NULL)
        return -1;
    //printf("mm_Init\n");
    return 0;
}

/*
 * extend_heap - Extend heap with free block and return its block pointer
 */
static void *extend_heap(size_t words) {
    char *bp;
    size_t size;

    // Allocate an even number of words to maintain alignment
    size = (words % 2) ? (words + 1) * WSIZE : words * WSIZE;
    if ((bp = mem_sbrk(size)) == (void *)-1)
        return NULL;

    /* Initialize free block header/footer and the epilogue header */
    PUT(HDRP(bp), PACK(size,GET_PREV_ALLOC(HDRP(bp)) ,0));         // Free block header
    PUT(FTRP(bp), PACK(size, GET_PREV_ALLOC(HDRP(bp)),0));         // Free block footer
    PUT(HDRP(NEXT_BLKP(bp)), PACK(0, 0,1)); // New epilogue header

    /* Coalesce if the previous block was free */
    return coalesce(bp);
}

/* 
 * find_idx - Find the appropriate index in the segregated free list array 
 */
static int find_idx(size_t size) {
    int idx;
    size_t adjusted_size = (size > MIN_BLCOK_SIZE) ? size : MIN_BLCOK_SIZE;

    for (idx = 0; idx < LISTLIMIT; idx++) {
        if (adjusted_size <= (1 << (idx + 4))) {  // 1 << 4는 16을 의미
            return idx;
        }
    }

    return idx;
}

static void insert_block(void* bp) {
    int idx = find_idx(GET_SIZE(HDRP(bp)));
    void *head = GET_LIST(idx);
    // 리스트가 비어있는 경우
    if (head != NULL) {
        PREC_FREE(head) = bp;
    }
    SUCC_FREE(bp) = head;
    PREC_FREE(bp) = NULL;
    
    SET_LIST(idx, bp);
    //printf("insert_block: bp %p, idx %d, size %zu\n", bp, idx, GET_SIZE(HDRP(bp)));
}

static void remove_block(void *bp) {
    
    // bp가 가리키고 있는 블럭의 크기를 통해 어떤 인덱스인지 계산
    int idx = find_idx(GET_SIZE(HDRP(bp)));
    // 해당 블럭 앞 뒤 포인터 얻기
    void *prev = PREC_FREE(bp);
    void *next = SUCC_FREE(bp);
    if ( prev!= NULL) {
        SUCC_FREE(prev) = next;
    } else {
        SET_LIST(idx, next);
    }

    if (next != NULL) {
        PREC_FREE(next) = prev;
    }
    //printf("remove_block: bp %p, idx %d, size %zu\n", bp, idx, GET_SIZE(HDRP(bp)));
}

static void *coalesce(void* bp) {
    size_t prev_alloc = GET_PREV_ALLOC(HDRP(bp));
    size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(bp)));
    size_t size = GET_SIZE(HDRP(bp));
    
    //1. 둘다 할당된 경우
    if (prev_alloc && next_alloc) {
        // No coalescing하고 free list에 넣기...
        //insert_block(bp);
        //return bp;
    } else if (prev_alloc && !next_alloc) {
        // 다음 블럭과 합치기
        remove_block(NEXT_BLKP(bp));
        size += GET_SIZE(HDRP(NEXT_BLKP(bp)));
        PUT(HDRP(bp), PACK(size,1 ,0));
        PUT(FTRP(bp), PACK(size,1 ,0));
    } else if (!prev_alloc && next_alloc) {
        // 이전 블럭과 합치기
        remove_block(PREV_BLKP(bp));
        size += GET_SIZE(HDRP(PREV_BLKP(bp)));
        bp = PREV_BLKP(bp);
        PUT(HDRP(bp), PACK(size, 1,0));
        PUT(FTRP(bp), PACK(size, 1,0));
    } else {
        // 다음, 이전 블럭과 합치기
        remove_block(PREV_BLKP(bp));
        remove_block(NEXT_BLKP(bp));
        size += GET_SIZE(HDRP(PREV_BLKP(bp))) + GET_SIZE(HDRP(NEXT_BLKP(bp)));
        bp = PREV_BLKP(bp);
        PUT(HDRP(bp), PACK(size, 1,0));
        PUT(FTRP(bp), PACK(size, 1,0));
    }
    //free된 블럭 리스트에 넣기
    insert_block(bp);
    //printf("coalesce: bp %p, size %zu\n", bp, size);
    return bp;
}

/* 
 * mm_malloc - Allocate a block with at least size bytes of payload 
 */
void *mm_malloc(size_t size) {
    size_t asize; //패딩까지 된 block size
    size_t extendsize; // 공간 없으면 늘릴 사이즈
    char *bp;

    if (size == 0)
        return NULL;

    // Adjust block size to include overhead and alignment reqs
    if (size <= DSIZE + WSIZE)
        asize = MIN_BLCOK_SIZE;
    else
        asize = DSIZE * ((size + (WSIZE) + (DSIZE-1)) / DSIZE);

    // Search the free list for a fit
    if ((bp = find_fit(asize)) != NULL) {
        place(bp, asize);
        //printf("mm_malloc: allocated %zu bytes at %p\n", asize, bp);
        return bp;
    }

    // No fit found. Get more memory and place the block
    extendsize = MAX(asize, CHUNKSIZE);
    if ((bp = extend_heap(extendsize / WSIZE)) == NULL)
        return NULL;
    place(bp, asize);
    //printf("mm_malloc: allocated %zu bytes at %p (extended)\n", asize, bp);
    return bp;
}

/*
 * mm_free - Free a block
 */
void mm_free(void *bp) {
    if (bp == NULL)
        return;
    void *next_block_hdrp;
    size_t size = GET_SIZE(HDRP(bp));
    PUT(HDRP(bp), PACK(size,GET_PREV_ALLOC(HDRP(bp)),0));
    PUT(FTRP(bp), PACK(size,GET_PREV_ALLOC(HDRP(bp)),0));

    next_block_hdrp = HDRP(NEXT_BLKP(bp));
    PUT(next_block_hdrp, GET(next_block_hdrp) & ~0x2);
    coalesce(bp);
    //printf("mm_free: freed block at %p\n", bp);
}


static void *find_fit(size_t asize) {
    void *bp;
    void *best_fit = NULL; // Pointer to the best fit block found
    size_t best_fit_size = (size_t)-1; // Initialize to maximum possible size

    // Start searching from the list corresponding to asize
    for (int i = find_idx(asize); i < LISTLIMIT; i++) {
        // Check if the list is empty to skip it
        if (GET_LIST(i) == NULL) {
            continue;
        }

        // Iterate through the free blocks in the current list
        for (bp = GET_LIST(i); bp != NULL; bp = SUCC_FREE(bp)) {
            //coalesce(bp);
            size_t block_size = GET_SIZE(HDRP(bp));

            // Check if the current block is a better fit than the previous best fit
            if (asize <= block_size && block_size < best_fit_size) {
                best_fit = bp;
                best_fit_size = block_size;

                // If we find a perfect fit, return immediately
                if (block_size == asize) {
                    return bp;
                }
            }
        }

        // If a best fit has been found in the current list, return it
        if (best_fit != NULL) {
            return best_fit;
        }
    }

    // If no fit is found, return NULL
    return NULL;
}



static void place(void *bp, size_t asize) {
    //할당하고자하는 블럭의 크기
    size_t csize = GET_SIZE(HDRP(bp));
    //remove_block(bp);
    //남은 블럭의 크기가 최소 블럭 크기보다 큰경우 -> asize로 할당하고 나서, 스플릿 필요
    if ((csize - asize) >= (2 * DSIZE)) {
        remove_block(bp);
        PUT(HDRP(bp), PACK(asize, 1, 1));
        PUT(FTRP(bp), PACK(asize, 1, 1));
        
        bp = NEXT_BLKP(bp);
        PUT(HDRP(bp), PACK(csize - asize, 1, 0));
        PUT(FTRP(bp), PACK(csize - asize, 1, 0));
        //insert_block(bp);
        coalesce(bp);
    } else {
    // 남은 블럭의 크기가 최소 블럭 크기보다 작은 경우 -> csize로 통째로 할당
        remove_block(bp);
        PUT(HDRP(bp), PACK(csize, 1, 1));
        PUT(FTRP(bp), PACK(csize, 1, 1));
        PUT(HDRP(NEXT_BLKP(bp)), GET(HDRP(NEXT_BLKP(bp))) | 0x2);
    }
    //printf("place: placed block at %p, asize %zu, csize %zu\n", bp, asize, csize);
}
void *mm_realloc(void *oldptr, size_t size) {
    size_t oldsize, newsize;
    void *newptr = oldptr;

    // 크기가 0이면 블록을 해제하고 NULL 반환
    if (size == 0) {
        mm_free(oldptr);
        return NULL;
    }

    // oldptr이 NULL이면 mm_malloc 호출
    if (oldptr == NULL) {
        return mm_malloc(size);
    }

    // 현재 블록의 크기 가져오기 (헤더의 크기 포함)
    oldsize = GET_SIZE(HDRP(oldptr));

    // 실제 필요한 크기 계산 (헤더를 포함한 최소 크기 보장)
    if (size <= DSIZE + WSIZE)
        newsize = MIN_BLCOK_SIZE;
    else
        newsize = DSIZE * ((size + (WSIZE) + (DSIZE-1)) / DSIZE);

    // 요청된 크기가 현재 블록보다 작거나 같은 경우
    if (newsize <= oldsize) {
        return oldptr;
    }

    // 인접한 다음 블록 확인
    void *next = NEXT_BLKP(oldptr);
    int next_alloc = GET_ALLOC(HDRP(next));
    size_t next_size = GET_SIZE(HDRP(next));

    // 인접한 블록과 병합하여 확장 가능한 경우
    if (!next_alloc && (oldsize + next_size) >= newsize) {
        // 병합하여 확장
        remove_block(next);
        size_t total_size = oldsize + next_size;
        int prev_alloc = GET_PREV_ALLOC(HDRP(oldptr));
        PUT(HDRP(oldptr), PACK(total_size, prev_alloc, 1));
        PUT(FTRP(oldptr), PACK(total_size, prev_alloc, 1));
        PUT(HDRP(NEXT_BLKP(oldptr)), GET(HDRP(NEXT_BLKP(oldptr))) | 0x3);
        return newptr;
    }

    // 새로운 블록 할당
    newptr = mm_malloc(size);
    if (newptr == NULL) {
        return NULL;
    }

    // 기존 데이터 복사 (헤더를 제외한 데이터 부분만 복사)
    if (newsize < oldsize) {
        memcpy(newptr, oldptr, newsize - WSIZE); // 헤더 제외
    } else {
        memcpy(newptr, oldptr, oldsize); // 헤더 제외
    }

    // 기존 블록 해제
    mm_free(oldptr);

    return newptr;
}


/*
 * mm-naive.c - The fastest, least memory-efficient malloc package.
 *
 * In this naive approach, a block is allocated by simply incrementing
 * the brk pointer.  A block is pure payload. There are no headers or
 * footers.  Blocks are never coalesced or reused. Realloc is
 * implemented directly using mm_malloc and mm_free.
 *
 * NOTE TO STUDENTS: Replace this header comment with your own header
 * comment that gives a high level description of your solution.
 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "memlib.h"
#include "mm.h"

/*explicit free list start*/
#define WSIZE 8              // Word and header/footer size (bytes)
#define DSIZE 16             // Double word size(bytes)
#define CHUNKSIZE (1 << 12)  // Extend heap by this amount (bytes)
#define MAX(x, y) ((x) > (y) ? (x) : (y))

#define PACK(size, prev_alloc, alloc) ((size) & ~(1 << 1) | ((prev_alloc) << 1) & ~(1) | (alloc))  // Pack size, prev allocated and allocated bit into a word (PACK(size, 0, 0))
#define PACK_PREV_ALLOC(val, prev_alloc) ((val) & ~(1 << 1) | ((prev_alloc) << 1))                 // Pack size and prev allocated bit into a word (PACK_PREV_ALLOC(GET(HDRP(bp)), 0))
#define PACK_ALLOC(val, alloc) ((val) | (alloc))                                                   // Pack size and allocated bit into a word (PACK_ALLOC(GET(HDRP(bp)), 0))

#define GET(p) (*(unsigned long*)(p))               // Read a word at address p
#define PUT(p, val) (*(unsigned long*)(p) = (val))  // Write a word at address p

#define GET_SIZE(p) (GET(p) & ~0x7)              // Size of the block at address p (header/footer).
#define GET_ALLOC(p) (GET(p) & 0x1)              // Is the block at address p (header/footer) allocated?
#define GET_PREV_ALLOC(p) ((GET(p) & 0x2) >> 1)  // Is the block before address p (header/footer) allocated?

#define HDRP(bp) ((char*)(bp)-WSIZE)                                 // Address of the block's header
#define FTRP(bp) ((char*)(bp) + GET_SIZE(HDRP(bp)) - DSIZE)          // Address of the block's footer. Only for free blk
#define NEXT_BLKP(bp) ((char*)(bp) + GET_SIZE(((char*)(bp)-WSIZE)))  // Next block
#define PREV_BLKP(bp) ((char*)(bp)-GET_SIZE(((char*)(bp)-DSIZE)))    // Prev block. Can only be used when prev_block is free.

#define GET_PRED(bp) (GET(bp))            // Free block's prev free block
#define SET_PRED(bp, val) (PUT(bp, val))  // Set free block's prev free block

#define GET_SUCC(bp) (GET(bp + WSIZE))            // Free block's next free block
#define SET_SUCC(bp, val) (PUT(bp + WSIZE, val))  // Set free block's next free block

#define MIN_BLK_SIZE (2 * DSIZE)  // Used for the sp place() function
/*explicit free list end*/

/* single word (4) or double word (8) alignment */
#define ALIGNMENT DSIZE

/* rounds up to the nearest multiple of ALIGNMENT */
#define ALIGN(size) (((size) + (ALIGNMENT - 1)) & ~0x7)

#define SIZE_T_SIZE (ALIGN(sizeof(size_t)))

static char* heap_listp;  // First mem block
static char* free_listp;  // First free mem block

static void* extend_heap(size_t words);
static void* coalesce(void* bp);
// static void *find_fit(size_t asize);
static void* find_fit_best(size_t asize);
static void* find_fit_first(size_t asize);
static void place(void* bp, size_t asize);
static void add_to_free_list(void* bp);
static void delete_from_free_list(void* bp);
double get_utilization();
void mm_check(const char*);
void mm_inspect(void* bp);

/*
    完成一个简单的分配器内存使用率统计
        user_malloc_size: 用户申请内存量
        heap_size: 分配器占用内存量
    HINTS:
        1. 在适当的地方修改上述两个变量，细节参考实验文档
        2. 在 get_utilization() 中计算使用率并返回
*/
size_t user_malloc_size = 0;
size_t heap_size = 0;
double get_utilization() {  // Memory use percent: user_malloc_size/heap_size
    double res = (double)user_malloc_size / heap_size;
    return res;
}

// Initialize the malloc package.
int mm_init(void) {
    free_listp = NULL;

    // 通过 mem_sbrk 请求 4 个字的内存(模拟 sbrk)
    if ((heap_listp = mem_sbrk(4 * WSIZE)) == (void*)-1) {
        heap_size += 4 * WSIZE;  // HACK: heap_size
        return -1;
    }
    // 分别作为填充块（为了对齐），序言块头/脚部，尾块
    // 并将 heap_listp 指针指向序言块使其作为链表的第一个节点
    PUT(heap_listp, 0);
    PUT(heap_listp + (1 * WSIZE), PACK(DSIZE, 1, 1));
    PUT(heap_listp + (2 * WSIZE), PACK(DSIZE, 1, 1));
    PUT(heap_listp + (3 * WSIZE), PACK(0, 1, 1));
    heap_listp += (2 * WSIZE);

    // 调用 extend_heap 函数向系统申请一个 CHUNKSIZE 的内存作为堆的初始内存
    if (extend_heap(CHUNKSIZE / WSIZE) == NULL)
        return -1;
    /* mm_check(__FUNCTION__);*/
    return 0;
}

// Allocate a block by incrementing the brk pointer. Always allocate a block whose size is a multiple of the alignment.
void* mm_malloc(size_t size) {
    /*printf("\n in malloc : size=%u", size);*/
    /*mm_check(__FUNCTION__);*/
    size_t newsize;
    size_t extend_size;
    void* bp;

    if (size == 0)
        return NULL;
    newsize = MAX(MIN_BLK_SIZE, ALIGN((size + WSIZE))); /*size+WSIZE(head_len)*/
    if ((bp = find_fit_first(newsize)) != NULL) {
        place(bp, newsize);
        user_malloc_size += GET_SIZE(HDRP(bp)) - WSIZE;
        return bp;
    }
    /*no fit found.*/
    extend_size = MAX(newsize, CHUNKSIZE);
    if ((bp = extend_heap(extend_size / WSIZE)) == NULL) {
        return NULL;
    }
    place(bp, newsize);
    user_malloc_size += GET_SIZE(HDRP(bp)) - WSIZE;
    return bp;
}

// Allocate a block by incrementing the brk pointer. Always allocate a block whose size is a multiple of the alignment. (best-fit)
void* mm_malloc_best(size_t size) {
    /*printf("\n in malloc : size=%u", size);*/
    /*mm_check(__FUNCTION__);*/
    size_t newsize;
    size_t extend_size;
    void* bp;

    if (size == 0)
        return NULL;
    newsize = MAX(MIN_BLK_SIZE, ALIGN((size + WSIZE))); /*size+WSIZE(head_len)*/
    if ((bp = find_fit_best(newsize)) != NULL) {
        place(bp, newsize);
        user_malloc_size += GET_SIZE(HDRP(bp)) - WSIZE;
        return bp;
    }
    /*no fit found.*/
    extend_size = MAX(newsize, CHUNKSIZE);
    if ((bp = extend_heap(extend_size / WSIZE)) == NULL) {
        return NULL;
    }
    place(bp, newsize);
    user_malloc_size += GET_SIZE(HDRP(bp)) - WSIZE;
    return bp;
}
// Freeing a block.
void mm_free(void* bp) {
    size_t size = GET_SIZE(HDRP(bp));
    size_t prev_alloc = GET_PREV_ALLOC(HDRP(bp));
    void* head_next_bp = NULL;

    user_malloc_size -= size - WSIZE;
    // mm_inspect(bp); // DEBUG
    PUT(HDRP(bp), PACK(size, prev_alloc, 0));
    PUT(FTRP(bp), PACK(size, prev_alloc, 0));
    /*printf("%s, addr_start=%u, size_head=%u, size_foot=%u\n",*/
    /*    __FUNCTION__, HDRP(bp), (size_t)GET_SIZE(HDRP(bp)), (size_t)GET_SIZE(FTRP(bp)));*/

    /*notify next_block, i am free*/
    // mm_inspect(NEXT_BLKP(bp)); // DEBUG
    head_next_bp = HDRP(NEXT_BLKP(bp));
    PUT(head_next_bp, PACK_PREV_ALLOC(GET(head_next_bp), 0));
    // if (GET_ALLOC(head_next_bp) == 0) { // coalesce will modify the tail
    //     PUT(FTRP(NEXT_BLKP(bp)), PACK_PREV_ALLOC(GET(head_next_bp), 0));
    // }

    // add_to_free_list(bp);

    coalesce(bp);
}

// Implemented simply in terms of mm_malloc and mm_free
void* mm_realloc(void* ptr, size_t size) {
    void* oldptr = ptr;
    void* newptr;
    size_t copySize;

    newptr = mm_malloc(size);
    if (newptr == NULL)
        return NULL;
    copySize = *(size_t*)((char*)oldptr - SIZE_T_SIZE);
    if (size < copySize)
        copySize = size;
    memcpy(newptr, oldptr, copySize);
    mm_free(oldptr);
    return newptr;
}

static void* extend_heap(size_t words) {
    /*get heap_brk*/
    char* old_heap_brk = mem_sbrk(0);
    size_t prev_alloc = GET_PREV_ALLOC(HDRP(old_heap_brk));

    /*printf("\nin extend_heap prev_alloc=%u\n", prev_alloc);*/
    char* bp;
    size_t size;
    size = (words % 2) ? (words + 1) * WSIZE : words * WSIZE;

    if ((long)(bp = mem_sbrk(size)) == -1) {
        return NULL;
    }

    heap_size += size;                        // HACK: heap_size
    PUT(HDRP(bp), PACK(size, prev_alloc, 0)); /*last free block*/
    PUT(FTRP(bp), PACK(size, prev_alloc, 0));

    PUT(HDRP(NEXT_BLKP(bp)), PACK(0, 0, 1)); /*break block*/
    return coalesce(bp);
}

// 将 bp 指向的空闲块与相邻块合并
static void* coalesce(void* bp) {
    // 首先从前一块的脚部和后一块的头部获取相应的分配状态。
    void* next_bp = NEXT_BLKP(bp);
    void* prev_bp = PREV_BLKP(bp);
    size_t prev_alloc = GET_PREV_ALLOC(HDRP(bp));
    size_t next_alloc = GET_ALLOC(HDRP(next_bp));
    size_t size = GET_SIZE(HDRP(bp));
    size_t next_size = GET_SIZE(HDRP(next_bp));
    // 根据 4 种不同情况作相应处理
    // 合并的过程中，要从空闲链表中删除合并前的空闲块并且插入合并后的空闲块。(bp 一开始就不在空闲链表中，所以不需要删除它)
    // 由于序言块和尾块的存在，不需要考虑边界条件，进行合并操作的块一定不会触及堆底和堆顶，因此不需要检查合并块位置。
    if (prev_alloc && next_alloc) {                 // * 前后都是已分配的块
        PUT(HDRP(next_bp), PACK(next_size, 0, 1));  // 修改后块块头
        PUT(HDRP(bp), PACK(size, 1, 0));            // 修改自身块头
        PUT(FTRP(bp), PACK(size, 1, 0));            // 修改自身块尾
    } else if (prev_alloc && !next_alloc) {         // * 前块已分，后块空闲
        size += next_size;
        delete_from_free_list(next_bp);
        PUT(FTRP(next_bp), PACK(size, 1, 0));  // 修改后块块尾
        PUT(HDRP(bp), PACK(size, 1, 0));       // 修改自身块头
    } else if (!prev_alloc && next_alloc) {    // * 前块空闲，后块已分
        size += GET_SIZE(HDRP(prev_bp));
        delete_from_free_list(prev_bp);
        PUT(FTRP(bp), PACK(size, 1, 0));            // 修改自身块尾
        PUT(HDRP(prev_bp), PACK(size, 1, 0));       // 修改前块块头
        PUT(HDRP(next_bp), PACK(next_size, 0, 1));  // 修改后块块头
        bp = prev_bp;
    } else {  // * 前后都是空闲
        size += GET_SIZE(HDRP(prev_bp)) + next_size;
        delete_from_free_list(prev_bp);
        delete_from_free_list(next_bp);
        PUT(HDRP(prev_bp), PACK(size, 1, 0));  // 修改前块块头
        PUT(FTRP(next_bp), PACK(size, 1, 0));  // 修改后块块尾
        bp = prev_bp;
    }
    add_to_free_list(bp);
    // 最后返回合并后的指针
    return bp;
}

// 首次匹配算法：遍历 freelist， 找到第一个合适的空闲块后返回
static void* find_fit_first(size_t asize) {
    void* cur = free_listp;
    while (cur != NULL) {
        if (GET_SIZE(HDRP(cur)) >= asize) {
            return cur;
        }
        cur = GET_SUCC(cur);
    }
    return NULL;
}

static void* find_fit_best(size_t asize) {
    /*
        最佳配算法
            遍历 freelist， 找到最合适的空闲块，返回

        HINT: asize 已经计算了块头部的大小
    */
    // mm_check(__FUNCTION__); // DEBUG
    void* cur = free_listp;
    void* res;
    size_t size;
    size_t min = 0;
    while (cur != NULL) {
        if ((size = GET_SIZE(HDRP(cur))) >= asize) {
            min = size;
            res = cur;
            break;
        }
        cur = GET_SUCC(cur);
    }
    if (min == 0)
        return NULL;
    while (cur != NULL) {
        size = GET_SIZE(HDRP(cur));
        if (size >= asize && size < min) {
            min = size;
            res = cur;
            break;
        }
        cur = GET_SUCC(cur);
    }
    if (min == 0)
        return NULL;
    else
        return res;
}

// 将一个空闲块转变为已分配的块
static void place(void* bp, size_t asize) {
    /*
        1. 若空闲块在分离出一个 asize 大小的使用块后，剩余空间不足空闲块的最小大小，
            则原先整个空闲块应该都分配出去
        2. 若剩余空间仍可作为一个空闲块，则原空闲块被分割为一个已分配块+一个新的空闲块
        3. 空闲块的最小大小已经 #define，或者根据自己的理解计算该值
    */
    // mm_inspect(bp); // DEBUG
    size_t blk_size = GET_SIZE(HDRP(bp));
    assert(!GET_ALLOC(HDRP(bp)));  // 不允许转变已经分配的块
    if (asize > blk_size) {        // 无法分配
        puts("Unable to place!");
        exit(-1);
    } else if (asize + MIN_BLK_SIZE > blk_size) {  // 剩余空间不足空闲块的最小大小，整个空闲块都分配出去
        // mm_inspect(bp); // DEBUG
        // mm_inspect(NEXT_BLKP(bp)); // DEBUG
        void* head_next_bp = HDRP(NEXT_BLKP(bp));
        delete_from_free_list(bp);
        PUT(HDRP(bp), PACK(blk_size, GET_PREV_ALLOC(HDRP(bp)), 1));
        assert(GET_ALLOC(head_next_bp));                        // 后块必已分配
        PUT(head_next_bp, PACK(GET_SIZE(head_next_bp), 1, 1));  // 修改后一个块的块头
        // mm_inspect(bp); // DEBUG
        // mm_inspect(NEXT_BLKP(bp)); // DEBUG
    } else {  // 原空闲块被分割为一个已分配块+一个新的空闲块
        // mm_inspect(bp); // DEBUG
        delete_from_free_list(bp);
        PUT(HDRP(bp), PACK(asize, GET_PREV_ALLOC(HDRP(bp)), 1));
        void* next = NEXT_BLKP(bp);
        PUT(HDRP(next), PACK(blk_size - asize, 1, 0));
        PUT(FTRP(next), PACK(blk_size - asize, 1, 0));
        add_to_free_list(next);
        // mm_inspect(bp); // DEBUG
        // mm_inspect(next); // DEBUG
    }
}

static void add_to_free_list(void* bp) {
    /*set pred & succ*/
    // printf("+ Adding %zx to free list...\n", bp); // DEBUG
    if (free_listp == NULL) {  // free_list empty
        SET_PRED(bp, 0);
        SET_SUCC(bp, 0);
        free_listp = bp;
    } else {
        SET_PRED(bp, 0);
        SET_SUCC(bp, (size_t)free_listp); /*size_t ???*/
        SET_PRED(free_listp, (size_t)bp);
        free_listp = bp;
    }
    // mm_check(__FUNCTION__); // DEBUG
}

static void delete_from_free_list(void* bp) {
    // printf("- Deleting %zx from free list...\n", bp); // DEBUG
    void* prev_free_bp = 0;
    void* next_free_bp = 0;
    if (free_listp == NULL)
        return;
    prev_free_bp = GET_PRED(bp);
    next_free_bp = GET_SUCC(bp);

    if (prev_free_bp == next_free_bp && prev_free_bp != 0) {
        /*mm_check(__FUNCTION__);*/
        /*printf("\nin delete from list: bp=%u, prev_free_bp=%u, next_free_bp=%u\n", (size_t)bp, prev_free_bp, next_free_bp);*/
    }
    if (prev_free_bp && next_free_bp) {  // 11
        SET_SUCC(prev_free_bp, GET_SUCC(bp));
        SET_PRED(next_free_bp, GET_PRED(bp));
    } else if (prev_free_bp && !next_free_bp) {  // 10
        SET_SUCC(prev_free_bp, 0);
    } else if (!prev_free_bp && next_free_bp) {  // 01
        SET_PRED(next_free_bp, 0);
        free_listp = (void*)next_free_bp;
    } else {  // 00
        free_listp = NULL;
    }
    // mm_check(__FUNCTION__); // DEBUG
}

void mm_check(const char* function) {
    printf("---cur func: %s :\n", function);
    char* bp = free_listp;
    int count_empty_block = 0;
    while (bp != NULL) {  // not end block;
        count_empty_block++;
        printf("addr_start：%zx, addr_end：%zx, size_head:%zu, size_foot:%zu, PRED=%zx, SUCC=%zx \n", (size_t)bp - WSIZE,
               (size_t)FTRP(bp), GET_SIZE(HDRP(bp)), GET_SIZE(FTRP(bp)), GET_PRED(bp), GET_SUCC(bp));
        ;
        bp = (char*)GET_SUCC(bp);
    }
    printf("empty_block num: %d\n\n", count_empty_block);
}

// Debug function: Check out the block at `bp`
void mm_inspect(void* bp) {
    size_t header = HDRP(bp);
    size_t is_alloc = GET_ALLOC(header);
    size_t prev_alloc = GET_PREV_ALLOC(header);
    printf("  bp %zx: prev=%zx, next=%zx\n    header: addr=%zx, size=%zd, alloc=%zd, prev_alloc=%zd\n", bp, prev_alloc ? 0 : PREV_BLKP(bp), NEXT_BLKP(bp), header, GET_SIZE(header), is_alloc, prev_alloc);
    if (!is_alloc) {  // Free block
        size_t footer = FTRP(bp);
        printf("    pred=%zx, succ=%zx\n", GET_PRED(bp), GET_SUCC(bp));
        printf("    footer: addr=%zx, size=%zd, alloc=%zd, prev_alloc=%zd\n", footer, GET_SIZE(footer), is_alloc, GET_PREV_ALLOC(footer));
    }
}
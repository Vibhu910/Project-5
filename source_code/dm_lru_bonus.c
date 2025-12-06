#include <linux/list.h>
#include <linux/slab.h>
#include "dm_cache.h"

#ifndef BLOCK_SHIFT
#define BLOCK_SHIFT 3  /* 8 sectors = 2^3 */
#endif

static sector_t prev_src_blkaddr = 0;
static unsigned int seq_count = 0;
static int in_scan_sequence = 0;

void
my_cache_hit(struct block_device *cache_blkdev, struct cacheblock *block,
        struct list_head *lru_head)
{
    list_move_tail(&block->list, lru_head);
    do_read(cache_blkdev, block->cache_block_addr);
}

struct cacheblock*
my_cache_miss(struct block_device *src_blkdev, struct block_device *cache_blkdev,
        sector_t src_blkaddr, struct list_head *lru_head)
{
    struct cacheblock *block;
    sector_t block_size = 1 << BLOCK_SHIFT;
    int is_sequential = 0;
    
    if (seq_count > 0 && src_blkaddr == prev_src_blkaddr + block_size) {
        is_sequential = 1;
    }
    
    if (in_scan_sequence) {
        if (is_sequential) {
            prev_src_blkaddr = src_blkaddr;
            do_read(src_blkdev, src_blkaddr);
            return NULL;
        }
        in_scan_sequence = 0;
        seq_count = 0;  /* Reset sequence count when exiting scan sequence */
        prev_src_blkaddr = src_blkaddr;  /* Update to current address for next comparison */
        /* Continue to cache this block */
    }
    
    if (is_sequential) {
        seq_count++;
        if (seq_count >= 10) {
            in_scan_sequence = 1;
            prev_src_blkaddr = src_blkaddr;
            do_read(src_blkdev, src_blkaddr);
            return NULL;
        }
    } else {
        seq_count = 1;
    }
    
    prev_src_blkaddr = src_blkaddr;
    
    block = list_first_entry(lru_head, struct cacheblock, list);
    list_move_tail(&block->list, lru_head);
    do_read(src_blkdev, src_blkaddr);
    do_write(cache_blkdev, block->cache_block_addr);
    
    return block;
}

void
init_lru(struct list_head *lru_head, unsigned int num_blocks)
{
    unsigned int i;
    struct cacheblock *block;
    
    INIT_LIST_HEAD(lru_head);
    
    for (i = 0; i < num_blocks; i++) {
        block = (struct cacheblock *)kvmalloc(sizeof(struct cacheblock), GFP_KERNEL);
        block->cache_block_addr = i << BLOCK_SHIFT;
        block->src_block_addr = 0;
        list_add(&block->list, lru_head);
    }
    
    prev_src_blkaddr = 0;
    seq_count = 0;
    in_scan_sequence = 0;
}

void
free_lru(struct list_head *lru_head)
{
    struct cacheblock *block;
    
    while (!list_empty(lru_head)) {
        block = list_first_entry(lru_head, struct cacheblock, list);
        list_del(&block->list);
        kvfree(block);
    }
}

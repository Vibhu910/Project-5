#include <linux/list.h>
#include <linux/slab.h>
#include "dm_cache.h"

void init_lru(struct list_head *lru_head, unsigned int num_blocks)
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
}

struct cacheblock* my_cache_miss(struct block_device *src_blkdev, struct block_device *cache_blkdev,
        sector_t src_blkaddr, struct list_head *lru_head)
{
    struct cacheblock *block;
    
    block = list_first_entry(lru_head, struct cacheblock, list);
    list_move_tail(&block->list, lru_head);
    do_read(src_blkdev, src_blkaddr);
    do_write(cache_blkdev, block->cache_block_addr);
    
    return block;
}

void my_cache_hit(struct block_device *cache_blkdev, struct cacheblock *block,
        struct list_head *lru_head)
{
    list_move_tail(&block->list, lru_head);
    do_read(cache_blkdev, block->cache_block_addr);
}

void free_lru(struct list_head *lru_head)
{
    struct cacheblock *block;
    
    while (!list_empty(lru_head)) {
        block = list_first_entry(lru_head, struct cacheblock, list);
        list_del(&block->list);
        kvfree(block);
    }
}

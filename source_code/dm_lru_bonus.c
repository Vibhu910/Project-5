#include <linux/list.h>
#include <linux/slab.h>
#include "dm_cache.h"

static sector_t prev_src_blkaddr = 0;
static unsigned int seq_count = 0;
static int in_scan_sequence = 0;

void my_cache_hit(struct block_device *cache_blkdev, struct cacheblock *block,
                  struct list_head *lru_head)
{
    list_move_tail(&block->list, lru_head);
    do_read(cache_blkdev, block->cache_block_addr);

    if (seq_count > 0 && block->src_block_addr == prev_src_blkaddr + 1)
        seq_count++;
    else
        seq_count = 1;

    prev_src_blkaddr = block->src_block_addr;

    if (seq_count >= 10)
        in_scan_sequence = 1;
}

struct cacheblock* my_cache_miss(struct block_device *src_blkdev,
                                 struct block_device *cache_blkdev,
                                 sector_t src_blkaddr,
                                 struct list_head *lru_head)
{
    int is_seq = (seq_count > 0 && src_blkaddr == prev_src_blkaddr + 1);

    if (in_scan_sequence) {
        if (is_seq) {
            prev_src_blkaddr = src_blkaddr;
            do_read(src_blkdev, src_blkaddr);
            return NULL;
        }
        in_scan_sequence = 0;
        seq_count = 1;
        prev_src_blkaddr = src_blkaddr;
    } else {
        seq_count = is_seq ? seq_count + 1 : 1;
        prev_src_blkaddr = src_blkaddr;

        if (seq_count >= 10) {
            in_scan_sequence = 1;
            do_read(src_blkdev, src_blkaddr);
            return NULL;
        }
    }

    struct cacheblock *block = list_first_entry(lru_head, struct cacheblock, list);
    list_move_tail(&block->list, lru_head);

    block->src_block_addr = src_blkaddr;

    do_read(src_blkdev, src_blkaddr);
    do_write(cache_blkdev, block->cache_block_addr);

    return block;
}

void init_lru(struct list_head *lru_head, unsigned int num_blocks)
{
    INIT_LIST_HEAD(lru_head);

    for (unsigned int i = 0; i < num_blocks; i++) {
        struct cacheblock *b = kvmalloc(sizeof(*b), GFP_KERNEL);
        b->cache_block_addr = i << BLOCK_SHIFT;
        b->src_block_addr = 0;
        list_add(&b->list, lru_head);
    }

    prev_src_blkaddr = 0;
    seq_count = 0;
    in_scan_sequence = 0;
}

void free_lru(struct list_head *lru_head)
{
    while (!list_empty(lru_head)) {
        struct cacheblock *b = list_first_entry(lru_head, struct cacheblock, list);
        list_del(&b->list);
        kvfree(b);
    }
}

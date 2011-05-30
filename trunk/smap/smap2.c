#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <pthread.h>

#include "smap2.h"
#include "tree.h"
#include "queue.h"

struct SMAP_ENT {
	union {
		SLIST_ENTRY(SMAP_ENT) mem_ent;
		RB_ENTRY(SMAP_ENT)    val_ent;
	};
	struct PAIR pair;
};

RB_HEAD(SMAP_TREE, SMAP_ENT);

struct BUCKET {
        uint64_t counter;
        struct SMAP_TREE root;
};
SLIST_HEAD(MPOOL_HEAD, SMAP_ENT);

struct SEGMENT {
		struct MPOOL_HEAD	*mpool;
		uint64_t	counter;
		int	bucket_num;
		struct BUCKET *bp;
		int pool_size;
        pthread_rwlock_t seg_lock;
};

#define MAXIMUM_CAPACITY (1 << 30)
#define MAX_SEGMENTS (1 << 16) 

static inline int
hash(int h) 
{
	// Spread bits to regularize both segment and index locations,
	// using variant of single-word Wang/Jenkins hash.
	h += (h <<  15) ^ 0xffffcd7d;
	h ^= (((unsigned int)h) >> 10);
	h += (h <<   3);
	h ^= (((unsigned int)h) >>  6);
	h += (h <<   2) + (h << 14);
	return h ^ (((unsigned int)h) >> 16);
}

static inline int
shash(char *str)
{
	unsigned int seed = 131; // 31 131 1313 13131 131313 etc..
	unsigned int hash = 0;
 
	while (*str)
	{
		hash = hash * seed + (*str++);
	}
 
	return (hash & 0x7FFFFFFF);
}

/* It support both number and string */
static inline int
cmp1(struct SMAP_ENT *a, struct SMAP_ENT *b)
{
	if (a->pair.type == b->pair.type) {
		if (a->pair.type == HASHTYPE_NUM) {
			int64_t diff = a->pair.ikey - b->pair.ikey;

			if (diff == 0)
				return (0);
			else if (diff < 0)
				return (-1);
			else
				return (1);
		} else {
			int c = a->pair.key_len - b->pair.key_len;
			if (c == 0) {
				c = strncmp(a->pair.skey, b->pair.skey, a->pair.key_len);
				return (c ? (c < 0 ? -1 : 1) : 0);
			} else 
				return (c < 0 ? -1 : 1);

		}
	} else {
		return (a->pair.type - b->pair.type);
	}
}


RB_PROTOTYPE_STATIC(SMAP_TREE, SMAP_ENT, val_ent, cmp1);
RB_GENERATE_STATIC(SMAP_TREE, SMAP_ENT, val_ent, cmp1);


static void
rdlock(pthread_rwlock_t *lock)
{
	int rc;
	rc = pthread_rwlock_rdlock(lock);
	if (rc != 0) {
		printf("lock! error: %d: %s \n", __LINE__, strerror(rc));
		exit(1);
	}
}

static void
wrlock(pthread_rwlock_t *lock)
{
	int rc;
//	printf("lock:%p\n",lock);
	rc = pthread_rwlock_wrlock(lock);
	if (rc != 0) {
		printf("lock! error: %d: %s \n", __LINE__, strerror(rc));
		exit(1);
	}
}

static void
unlock(pthread_rwlock_t *lock)
{
	int rc;
//	printf("unlock:%p\n",lock);
	rc = pthread_rwlock_unlock(lock);
	if (rc != 0) {
		printf("lock! error: %d: %s \n", __LINE__, strerror(rc));
		exit(1);
	}
}

static struct SMAP_ENT *
mpool_alloc(struct SEGMENT *sp)
{
	struct SMAP_ENT *entry;
	if (!SLIST_EMPTY(sp->mpool)) {
		entry = SLIST_FIRST(sp->mpool);
		SLIST_REMOVE_HEAD(sp->mpool, mem_ent);
		sp->pool_size--;
	} else {
		entry = (struct SMAP_ENT *)malloc(sizeof(struct SMAP_ENT));
	}
	return entry;
}

static void
mpool_free(struct SEGMENT *sp, struct SMAP_ENT *ptr)
{
	if (sp->pool_size < sp->bucket_num ) {
		SLIST_INSERT_HEAD(sp->mpool, ptr, mem_ent);
		sp->pool_size++;
	} else {
		free(ptr);
	}
}
/*
 * hash_type: number or string
 * level: The concurrency level
 */
struct smap *
smap_init(int capacity , int level, int pool_size, int hash_type)
{
	struct BUCKET *bp;
	struct smap *mp;
	int i, j;
	int rc;
	int sshift = 0;
	int ssize = 1;
	int cap, c;
	pthread_rwlockattr_t attr;

	mp = (struct smap *)malloc(sizeof(struct smap));

	while ( ssize < level ) {
		++sshift;
		ssize <<= 1;
	}

	mp->seg_shift = 32 - sshift;
	mp->seg_mask = ssize - 1;
	mp->hash_type = hash_type;

	if (capacity > MAXIMUM_CAPACITY)
		capacity = MAXIMUM_CAPACITY;
	c = capacity / ssize;
	if (c * ssize < capacity)
		++c;
	cap = 1;
	while (cap < c)
		cap <<= 1;

	mp->seg = (struct SEGMENT *)malloc(sizeof(struct SEGMENT)*ssize);
	mp->seg_num = ssize;

	for (i = 0; i < ssize; i++) {
		rc = pthread_rwlockattr_init(&attr);
		rc = pthread_rwlock_init(&(mp->seg[i].seg_lock), &attr);
		if (rc != 0)
			return (NULL);
		bp = (struct BUCKET *)malloc(sizeof(struct BUCKET) * cap);
		if (bp == NULL)
		        return (NULL);

		for (j = 0; j < cap; j++) {
			bp[j].counter = 0;
			RB_INIT(&(bp[j].root));
		}
		mp->seg[i].bucket_num = cap;	
		mp->seg[i].bp = bp;
		mp->seg[i].mpool = (struct MPOOL_HEAD *)malloc(sizeof(struct MPOOL_HEAD));
		SLIST_INIT(mp->seg[i].mpool);
		if (pool_size == 0)
			pool_size = cap/2;
		for (j = 0; j < pool_size; j++) {
			struct SMAP_ENT *new_mem_ent = (struct SMAP_ENT *)malloc(sizeof(struct SMAP_ENT));
			SLIST_INSERT_HEAD(mp->seg[i].mpool, new_mem_ent, mem_ent);
		}
		mp->seg[i].pool_size = pool_size;
	}
	return (mp);
}

/*
 * clear a map, it would not always lock, just realloc the bucket and mpool.
 */
void
smap_clear(struct smap *mp, int start)
{
	int i, j;
	struct SEGMENT *sp;
	struct BUCKET *old_bp;
	struct BUCKET *new_bp;
	struct BUCKET *bp;
	struct SMAP_ENT *np, *tnp;
	int pool_size = 0;
	struct MPOOL_HEAD	*mpool, *tmpool;
	
	mpool = (struct MPOOL_HEAD *)malloc(sizeof(struct MPOOL_HEAD));

	for (i = 0; i < mp->seg_num; i++) {
		SLIST_INIT(mpool);
		sp = &(mp->seg[(i + start) % (mp->seg_num)]);

		new_bp = (struct BUCKET *)malloc(sp->bucket_num * sizeof(struct BUCKET));
		for (j = 0; j < sp->bucket_num; j++) {
			new_bp[j].counter = 0;
			RB_INIT(&(new_bp[j].root));
		}

		/* alloc a new bucket array and delete the old one. */
		wrlock(&(sp->seg_lock));
		old_bp = sp->bp;
		sp->bp = new_bp;
		unlock(&(sp->seg_lock));
		for (j = 0; j < sp->bucket_num; j++) {
			bp = &(old_bp[j]);
			for (np = RB_MIN(SMAP_TREE, &(bp->root));
				np && ((tnp) = RB_NEXT(SMAP_TREE, &(bp->root), np), 1);
				np = tnp) {
				RB_REMOVE(SMAP_TREE, &(bp->root), np);
				SLIST_INSERT_HEAD(mpool, np, mem_ent);
				
				if (pool_size < sp->bucket_num)
					pool_size++;
				else
					free(np);
			}
		}
		wrlock(&(sp->seg_lock));
		free(old_bp);
		
		/* Process the mpool. */
		if (sp->pool_size >= pool_size) {
			unlock(&(sp->seg_lock));
			SLIST_FOREACH_SAFE(np, mpool, mem_ent, tnp) {
				free(np);
			}
		} else {
			tmpool = sp->mpool;
			sp->mpool = mpool;
			sp->pool_size = pool_size;
			unlock(&(sp->seg_lock));
			SLIST_FOREACH_SAFE(np, tmpool, mem_ent, tnp) {
				free(np);
			}
			free(tmpool);
			mpool = tmpool;
		}
		pool_size = 0;
	}
}


static struct SEGMENT *
get_segment(struct smap *mp, int hash)
{
	int seg_hash;
	
	/* right shift 32bit problem */
	uint64_t shit = hash & 0x7FFFFFFF;

	seg_hash = (shit >> mp->seg_shift) & (mp->seg_shift);
	return mp->seg + seg_hash;
}

static struct BUCKET *
get_bucket(struct SEGMENT *sp, int hash)
{
	int index = hash & (sp->bucket_num - 1);
	return &(sp->bp[index]);
}

int
smap_insert(struct smap *mp, struct PAIR *pair, int lock)
{
	struct BUCKET *bp;
	struct SMAP_ENT *entry;
	struct SMAP_ENT *rc;
	int h;
	struct SEGMENT *sp;

	if (mp == NULL && pair == NULL)
		return (SMAP_GENERAL_ERROR);
	
	if (pair->type == HASHTYPE_NUM)
		h = hash((int)pair->ikey);
	else if (pair->type == HASHTYPE_STR)
		h = hash((int)pair->ikey);
	else
		return (SMAP_GENERAL_ERROR);

	sp = get_segment(mp, h);
	bp = get_bucket(sp, h);
	
	if (lock)
		wrlock(&(sp->seg_lock));
	
	entry = mpool_alloc(sp);
	if (entry == NULL) {
		if (lock)
			unlock(&(sp->seg_lock));
		return (SMAP_OOM);
	}
	
	memcpy(entry, pair, sizeof(struct PAIR));
	
	sp->counter++;
	rc = RB_INSERT(SMAP_TREE, &(bp->root), entry);
	if (lock)
		unlock(&(sp->seg_lock));

	if (rc == NULL)
		return (SMAP_OK);
	else
		return (SMAP_DUPLICATE_KEY);
}

/*
 * it won't free the value, do it yourself.
 */

int
smap_delete(struct smap *mp, uint64_t ikey, int lock)
{
	struct BUCKET *bp;
	struct SMAP_ENT entry;
	struct SMAP_ENT *res;
	int h;
	struct SEGMENT *sp;

	if (mp == NULL)
		return (SMAP_GENERAL_ERROR);

	h = hash((int)ikey);

	sp = get_segment(mp, h);
	bp = get_bucket(sp, h);
	
	entry.pair.ikey = ikey;
	
	if (lock)
		wrlock(&(sp->seg_lock));
	
	res = RB_FIND(SMAP_TREE, &(bp->root), &entry);
	if (res == NULL) {
		if (lock)
			unlock(&(sp->seg_lock));
		return (SMAP_NONEXISTENT_KEY);
	}
	RB_REMOVE(SMAP_TREE, &(bp->root), res);

	bp->counter--;

	mpool_free(sp, res);
	if (lock)
		unlock(&(sp->seg_lock));
	return (SMAP_OK);
}

uint64_t
smap_get_elm_num(struct smap *mp)
{
	uint64_t c = 0;
	int i;
		
	if (mp == NULL)
		return (SMAP_GENERAL_ERROR);
	
	for (i = 0; i < mp->seg_num; i++) {
		c += mp->seg[i].counter;
	}
	return c;
}

/*
 * it won't free the value, do it yourself.
 */

void *
smap_get(struct smap *mp, uint64_t ikey)
{
	struct BUCKET *bp;
	struct SMAP_ENT entry;
	struct SMAP_ENT *rc;
	struct SMAP_ENT *np;
	int h;
	struct SEGMENT *sp;

	if (mp == NULL)
		return (NULL);

	h = hash((int)ikey);
	
	sp = get_segment(mp, h);
	bp = get_bucket(sp, h);
	
	entry.pair.ikey = ikey;
	
	rdlock(&(sp->seg_lock));
	rc = RB_FIND(SMAP_TREE, &(bp->root), &entry);
	unlock(&(sp->seg_lock));
	if (rc == NULL) {
		return (NULL);
	} else {
		return (rc->pair.data);
	}
}

/*
 * it won't free the value, do it yourself.
 */

int
smap_update(struct smap *mp, uint64_t ikey, void *data)
{
	struct BUCKET *bp;
	struct SMAP_ENT entry;
	struct SMAP_ENT *rc;
	int h;
	struct SEGMENT *sp;

	if (mp == NULL)
		return (SMAP_GENERAL_ERROR);

	h = hash((int)ikey) % (mp->bucket_num);
	sp = get_segment(mp, h);
	bp = get_bucket(sp, h);
	
	entry.pair.ikey = ikey;
	
	rdlock(&(sp->seg_lock));
	rc = RB_FIND(SMAP_TREE, &(bp->root), &entry);
	
	/* if no entry */
	if (rc == NULL) {
		unlock(&(sp->seg_lock));
		return (SMAP_NONEXISTENT_KEY);
	} else {
		rc->pair.data = data;
		unlock(&(sp->seg_lock));
		return (SMAP_OK);
	}
	
}

/*
 * it won't free the value, do it yourself.
 */
int
smap_traverse(struct smap *mp, int (*routine)(struct smap *, struct PAIR *),uint32_t start)
{
	struct BUCKET *bp;
	struct SMAP_ENT *np;
	struct SMAP_ENT *tnp;
	int i, j;
	int rc;
	struct SEGMENT *sp;
	
	if (mp == NULL)
		return (SMAP_GENERAL_ERROR);
	if (start >= mp->seg_num)
		return (SMAP_GENERAL_ERROR);
		
	for (i = 0; i < mp->seg_num; i++) {
		sp = &(mp->seg[(i + start) % (mp->seg_num)]);
		bp = sp->bp;
		wrlock(&(sp->seg_lock));
		for (j = 0; j < sp->bucket_num; j++) {
			bp = &(sp->bp[j]);

			for (np = RB_MIN(SMAP_TREE, &(bp->root));
				np && ((tnp) = RB_NEXT(SMAP_TREE, &(bp->root), np), 1);
				np = tnp) {
				rc = routine(mp, &np->pair);
				if (rc == SMAP_BREAK) {
					unlock(&(sp->seg_lock));
					goto out;
				}
			}
		}
		unlock(&(sp->seg_lock));
	}
	out:
	return 0;
}

int
test(struct smap *mp, uint64_t ikey, void *value)
{
	printf("ikey = %ld, value = %s\n", ikey, (char *)value);

	SMAP_OK;
}

int
test1(struct smap *mp, uint64_t ikey, void *value)
{
	if (SMAP_OK == smap_delete(mp, ikey, 0)) {
		printf("ikey = %ld, value = %s\n", ikey, (char *)value);
	} else {
		/* unreachable because travser won't reached */
		printf("no ikey!\n");
	}
	SMAP_OK;
}

/*
int
main(void)
{
	struct smap* map;
	int i;
	int rc;

	map = smap_init(1024, 10, 0, 0);

	if (map == NULL)
		printf("error map NULL \n");

	for (i = 0; i < 10240; i++) {
		rc = smap_insert(map, i, "haha", 1);
		if (rc < 0){
			printf("i: %d, error: %d\n", i, rc);
			break;
		}
	}
	
	smap_clear(map, 0);
	
	for (i = 0; i < 10240; i++) {
		rc = smap_insert(map, i, "hoho", 1);
		if (rc < 0){
			printf("i: %d, error: %d\n", i, rc);
			break;
		}
	}
	smap_traverse(map, test1, 0);
	smap_traverse(map, test, 0);
	return (0);
}
*/

int
main(void)
{
	printf("%d, %d\n", sizeof(struct PAIR), sizeof(struct SMAP_ENT));
	return 0;
}
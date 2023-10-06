#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include <string.h>

#include "chash.h"
#include "fatal.h"

#define BKT_EMPTY	0
#define BKT_ELEMENT	1
#define BKT_DELETED	2

#define RWLOCK_RDL(bkt)								\
	do {									\
		if (pthread_rwlock_rdlock(&bkt->lk) != 0)			\
			FATAL("failed to acquire read lock");			\
	} while (0);

#define RWLOCK_WRL(bkt)								\
	do {									\
		if (pthread_rwlock_wrlock(&bkt->lk) != 0)			\
			FATAL("failed to acquire write lock");			\
	} while (0);

#define RWLOCK_UNL(bkt)								\
	do {									\
		if (pthread_rwlock_unlock(&bkt->lk) != 0)			\
			FATAL("failed to return lock");				\
	} while (0);

/*
 * three states: empty, element, deleted
 *
 * empty -> element -> deleted
 *            ^          |
 *            +----------+
 *
 * key of bucket never changes after insert
 *
 * if insert is still possible, insert should retry: this is the case after we
 * try inserting into empty bucket and some other thread is in insert
 * contention
 *
 * inserting: find either empty or deleted corresponding key, try 
 *
 *
 */

/*
 * concurrent hash table implementation
 *
 * hashing: murmur hash
 *
 * keys: size of words
 *
 * how do we do size compression of keys
 *
 * two-word CAS operation?
 *
 *
 * maybe start easy with locks
 * 
 */

inline size_t conn_chash_hash(const struct conn_id *id)
{
	uint32_t hash;

	// FIXME use murmurhash or something
	hash = (id->host_ip ^ id->client_ip ^ id->host_port ^ id->client_port);

	return (hash);
}

int conn_chash_cas(struct conn_bkt *bkt, int occ, const struct conn_id *key,
    const struct conn_info *val, int *curr_occ, struct conn_id *curr_key,
    struct conn_info *curr_val)
{
	int ret;

	ret = -1;

	RWLOCK_WRL(bkt)
	{
		if (curr_occ != NULL &&
		    *curr_occ != bkt->occ) {
			goto end;
		}

		if (curr_key != NULL &&
		    memcmp(&bkt->k, curr_key, sizeof(*curr_key))) {
			goto end;
		}

		if (curr_val != NULL &&
		    memcmp(&bkt->v, curr_val, sizeof(*curr_val))) {
			goto end;
		}

		bkt->occ = occ;
		if (key != NULL)
			memcpy(&bkt->k, key, sizeof(*key));
		if (val != NULL)
			memcpy(&bkt->v, val, sizeof(*val));

end:
		if (curr_occ != NULL)
			*curr_occ = bkt->occ;
		if (curr_key != NULL)
			memcpy(curr_key, &bkt->k, sizeof(*curr_key));
		if (curr_val != NULL)
			memcpy(curr_val, &bkt->v, sizeof(*curr_val));
	}
	RWLOCK_UNL(bkt)

	return (ret);
}

int conn_chash_tbl_init(struct conn_chash_tbl *tbl, unsigned int sz_pow)
{
	size_t i;

	PRECOND(sz_pow > 0 && sz_pow <= 8*sizeof(uint32_t));

	tbl->sz = 1 << sz_pow;
	tbl->pow = sz_pow;

	// allocate buckets
	if ((tbl->bkts = calloc(tbl->sz, sizeof(struct conn_bkt))) == NULL)
		FATAL("failed to allocate buckets");

	for (i = 0; i < tbl->sz; i++) {
		if (pthread_rwlock_init(&tbl->bkts[i].lk, NULL) != 0)
			FATAL("failed to initialize bucket lock");
	}

	// initialize bucket locks
	return (0);
}

int conn_chash_tbl_insert(struct conn_chash_tbl *tbl,
    const struct conn_id *key, struct conn_info *val)
{
	uint32_t hash;
	size_t i;
	struct conn_bkt *bkt;
	struct conn_id curr_key;
	int done, insert, ret, curr_occ;

	ret = -1;
	done = insert = 0;

	hash = conn_chash_hash(key);
	hash >>= (8*sizeof(hash) - tbl->pow);

	for (i = hash; !done; i = (i + 1) % tbl->sz) {
		bkt = &tbl->bkts[i];

		RWLOCK_RDL(bkt)
		{
			curr_occ = bkt->occ;
			curr_key = bkt->k;
		}
		RWLOCK_UNL(bkt)

		switch (curr_occ) {
		case BKT_EMPTY:
			insert = 1;
			break;
		case BKT_DELETED:
			if (!memcmp(&curr_key, key, sizeof(*key))) {
				insert = 1;
			}
			else {
				done = 1;
				insert = 0;
			}
			break;
		default:
			insert = 0;
		}

		if (insert) {
			while (curr_occ == BKT_EMPTY ||
			    (curr_occ == BKT_DELETED &&
			     !memcmp(&curr_key, key, sizeof(*key)))) {
				ret = conn_chash_cas(bkt, BKT_ELEMENT, key, val,
				    &curr_occ, &curr_key, NULL);

				if (ret == 0)
					done = 1;
			}
		}

		if ((i + 1) % tbl->sz == hash) {
			done = 1;
		}
	}

	return (ret);
}

int conn_chash_tbl_get(struct conn_chash_tbl *tbl, const struct conn_id *key,
    struct conn_info **out)
{
	uint32_t hash;
	size_t i;
	struct conn_bkt *bkt;
	int ret, done;

	ret = -1;
	*out = NULL;
	done = 0;

	hash = conn_chash_hash(key);
	hash >>= (8*sizeof(hash) - tbl->pow);

	for (i = hash; !done; i = (i + 1) % tbl->sz) {
		bkt = &tbl->bkts[i];

		RWLOCK_RDL(bkt)
		{
			if (bkt->occ == BKT_EMPTY) {
				done = 1;
			}
			else if (bkt->occ == BKT_ELEMENT &&
			    !memcmp(&bkt->k, key, sizeof(*key))) {
				*out = &bkt->v;
				done = 1;
				ret = 0;
			}
		}
		RWLOCK_UNL(bkt)

		if ((i + 1) % tbl->sz == hash) {
			done = 1;
		}
	}

	return (ret);
}

int conn_chash_tbl_swap(struct conn_chash_tbl *tbl, const struct conn_id *key,
    struct conn_info *val, struct conn_info *out)
{
	uint32_t hash;
	size_t i;
	struct conn_bkt *bkt;
	struct conn_info curr_val;
	// struct conn_id curr_key;
	int ret, done, swap, curr_occ;

	ret = -1;
	swap = done = 0;

	hash = conn_chash_hash(key);
	hash >>= (8*sizeof(hash) - tbl->pow);

	for (i = hash; !done; i = (i + 1) % tbl->sz) {
		bkt = &tbl->bkts[i];

		RWLOCK_RDL(bkt)
		{
			curr_occ = bkt->occ;
			if (curr_occ == BKT_EMPTY) {
				done = 1;
			}
			else if (curr_occ == BKT_ELEMENT &&
			    !memcmp(&bkt->k, key, sizeof(*key))) {
				curr_val = bkt->v;
				// curr_key = bkt->k;
				swap = 1;
			}
		}
		RWLOCK_UNL(bkt)

		if (swap) {
			while (BKT_ELEMENT == curr_occ &&
			    // XXX relies on assumption that keys never move...
			    (ret = conn_chash_cas(bkt, BKT_ELEMENT, NULL, val,
			        &curr_occ, NULL /* XXX &curr_key */, NULL))
			    == -1) {};

			done = 1;
		}

		if ((i + 1) % tbl->sz == hash) {
			done = 1;
		}
	}

	if (ret == 0) {
		*out = curr_val;
	}

	return (ret);
}

int conn_chash_tbl_delete(struct conn_chash_tbl *tbl, const struct conn_id *key)
{
	uint32_t hash;
	size_t i;
	struct conn_bkt *bkt;
	// struct conn_id curr_key;
	int ret, done, found, curr_occ;

	ret = -1;
	found = 0;
	done = 0;

	hash = conn_chash_hash(key);
	hash >>= (8*sizeof(hash) - tbl->pow);

	for (i = hash; !done; i = (i + 1) % tbl->sz) {
		bkt = &tbl->bkts[i];

		RWLOCK_RDL(bkt)
		{
			if (BKT_EMPTY == (curr_occ =  bkt->occ))
				done = 1;
			else if (!memcmp(&bkt->k, key, sizeof(*key))) {
				// curr_key = bkt->k;
				found = 1;
			}
		}
		RWLOCK_UNL(bkt)

		if (found) {
			// XXX depends on assumption that keys aren't moved
			while (BKT_DELETED != curr_occ) {
				ret = conn_chash_cas(bkt, BKT_DELETED, NULL,
				    NULL, &curr_occ, NULL /* XXX &curr_key */, NULL);
			}

			done = 1;
		}

		if ((i + 1) % tbl->sz == hash) {
			done = 1;
		}
	}

	return (ret);
}

int conn_chash_tbl_update(struct conn_chash_tbl *tbl, const struct conn_id *key,
    int (*fun)(const struct conn_info *, struct conn_info *))
{
	uint32_t hash;
	size_t i;
	struct conn_bkt *bkt;
	struct conn_info curr_val, new_val;
	// XXX struct conn_id curr_key;
	int ret, found, done, curr_occ;

	ret = -1;
	found = done = 0;

	hash = conn_chash_hash(key);
	hash >>= (8*sizeof(hash) - tbl->pow);

	for (i = hash; !done; i = (i + 1) % tbl->sz) {
		bkt = &tbl->bkts[i];

		RWLOCK_RDL(bkt)
		{
			curr_occ = bkt->occ;
			if (curr_occ == BKT_EMPTY) {
				done = 1;
			}
			else if (BKT_ELEMENT == curr_occ &&
			    !memcmp(&bkt->k, key, sizeof(*key))) {
				curr_val = bkt->v;
				// XXX curr_key = bkt->k;
				found = 1;
			}
		}
		RWLOCK_WRL(bkt)

		if (found) {
			while (curr_occ == BKT_ELEMENT && ret != 0) {
				if ((ret = fun(&curr_val, &new_val)) != 0)
					break;
				// XXX assumption that keys aren't moved
				ret = conn_chash_cas(bkt, BKT_ELEMENT, NULL,
				    &new_val, &curr_occ,
				    NULL /* XXX &curr_key */, &curr_val);
			}

			done = 1;
		}

		if ((i + 1) % tbl->sz == hash) {
			done = 1;
		}
	}

	return (ret);
}

int conn_chach_tbl_key_init(struct conn_chash_tbl *tbl,
    const struct conn_id *key, int (*init_fun)(struct conn_info *))
{
	uint32_t hash;
	size_t i;
	struct conn_bkt *bkt;
	struct conn_id curr_key;
	int ret, done, insert, curr_occ;

	ret = -1;
	done = insert = 0;

	hash = conn_chash_hash(key);
	hash >>= (8*sizeof(hash) - tbl->pow);

	for (i = hash; !done; i = (i + 1) % tbl->sz) {
		bkt = &tbl->bkts[i];

		RWLOCK_RDL(bkt)
		{
			curr_occ = bkt->occ;
			curr_key = bkt->k;
		}
		RWLOCK_UNL(bkt)

		switch (curr_occ) {
		case BKT_EMPTY:
			insert = 1;
			break;
		case BKT_DELETED:
			if (!memcmp(&curr_key, key, sizeof(*key)))
				insert = 1;
			else {
				insert = 0;
				done = 1;
			}
			break;
		default:
			insert = 0;
		}

		if (insert) {
			while (curr_occ == BKT_EMPTY ||
			    (curr_occ == BKT_DELETED &&
			     !memcmp(&curr_key, key, sizeof(*key)))) {
				// TODO break out?
				RWLOCK_WRL(bkt)
				{
					/* occ hasn't changed, so we are still
					 * primed for insert */
					if (bkt->occ == curr_occ) {
						if (curr_occ == BKT_EMPTY)
							memcpy(&bkt->k, key, sizeof(*key));

						ret = init_fun(&bkt->v);

						if (ret == 0) {
							bkt->occ = BKT_ELEMENT;
							done = 1;
						}
					}
					else {
						curr_occ = bkt->occ;
						curr_key = bkt->k;
					}
				}
				RWLOCK_UNL(bkt)
			}
		}

		if ((i + 1) % tbl->sz == hash) {
			done = 1;
		}
	}

	return (ret);
}

int conn_chash_tbl_resize(struct conn_chash_tbl *tbl __attribute__((unused)),
    unsigned int pow __attribute__((unused)))
{
	int ret;

	// not implemented...
	ASSERT(0);

	ret = -1;

	return (ret);
}

/* how to do resize
 *
 * divide into blocks with constant size?
 *
 * lock for each block
 *
 * moved/still here boolean
 *
 * how do we sync between table access and move?
 *
 * check moving status?
 *
 * how do we know any thread
 *
 * moved status on element?
 *
 * all accesses requires read permission anyways, we can check moved status on
 * each access?
 *
 * idx2block: Int -> Block
 *
 * each block moved by each thread?
 *
 * check block moved status
 *
 * if there is another table, check move status of current block, if it is
 * moved, access next table. if not, move it and continue with next table
 *
 * move status:
 *
 * not moved,
 * moving,
 * moved
 *
 * counter keeping track of number of blocks left to move, when it hits 0,
 * deallocate old table and reassign pointers?
 *
 * moving pointers to new table and moved blocks might be a bit of a problem
 * what is the serialization point, how do we not leave our data structure in
 * an inconsistent state here?
 *
 * we could just solve it with a global reader/writers lock instead
 *
 * access new table if available
 *
 * if new table is avalable
 *
 * each block has move status
 *
 * on each access, check that move status hasn't changed or is in moving status
 * need to check before and after
 *
 * 
 */

if (new table) {
	move_block

}


void conn_chash_tbl_destroy(struct conn_chash_tbl *tbl)
{
	size_t i;
	struct conn_bkt *bkt;

	for (i = 0; i < tbl->sz; i++) {
		bkt = &tbl->bkts[i];

		if (pthread_rwlock_destroy(&bkt->lk) != 0)
			FATAL("failed to destroy rw lock");
	}

	free(tbl->bkts);
}

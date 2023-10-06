#ifndef __CHASH_H
#define __CHASH_H
//
//struct rlly_chash_bkt {
//	uint64_t		ik
//	void			*k;
//	void			*v;
//	pthread_rwlock_t	lk;
//}
//
//typedef rlly_chash_cmprf_p int (*)(void *, void *);
//typedef rlly_chash_hashf_p size_t (*)(void *);
//
//struct rlly_chash {
//	size_t			cap;
//	struct rlly_chash_bkt	*bkts;
//	rlly_chash_cmprf_p	cmprf;
//	rlly_chash_hashf_p	hashf;
//	pthread_rwlock_t	reallock;
//}
//
//int	rlly_chash_init(size_t, rlly_chash_eqlsf_p, rlly_chash_hashf_p);
//int	rlly_chash_insert(struct rlly_chash *, const void *, const void *);
//int	rlly_chash_get(struct rlly_chash *, const void *, void **);
//int	rlly_chash_swap(struct rlly_chash *, const void *, void *, void **);
//int	rlly_chash_delete(struct rlly_chash *, const void *);
//int	rlly_chash_update(struct rlly_chash *, const void *,
//	    int (*)(void *, void **));
//int	rlly_chash_resize(struct rlly_chash *, size_t);
//void	rlly_chash_destroy(struct rlly_chash *);
//
//#define RLLY_CHASH_BKT_T(name, key_t, val_t)				\
//struct name {								\
//	key_t			k;					\
//	val_t			v;					\
//	int			mvd;					\
//	pthread_rwlock_t	lk;					\
//}
//
//#define RLLY_CHASH_TBL_T(name, bkt_t, hash_f_t, eqlsf_f_t)		\
//struct name {								\
//	pthread_rwlock_t	glk;					\
//	bkt_t			*bkts;					\
//	hashf_t			hashf;					\
//	eqlsf_t			eqf;					\
//}

struct conn_id {
	uint32_t host_ip;
	uint16_t host_port;
	uint32_t client_ip;
	uint16_t client_port;
};

struct conn_info {
	int info;
};

struct conn_bkt {
	pthread_rwlock_t	lk;
	int			occ;
	struct conn_id		k;
	struct conn_info	v;
};

struct conn_chash_tbl {
	size_t			sz;
	unsigned int		pow;
	struct conn_bkt		*bkts;
	size_t			rsz_sz;
	unsigned int		rsz_pow;
	struct conn_bkt		*rsz_bkts;
};

int	conn_chash_tbl_init(struct conn_chash_tbl *, unsigned int);
int	conn_chash_tbl_insert(struct conn_chash_tbl *, const struct conn_id *, struct conn_info *);
int	conn_chash_tbl_get(struct conn_chash_tbl *, const struct conn_id *, struct conn_info **);
int	conn_chash_tbl_swap(struct conn_chash_tbl *, const struct conn_id *, struct conn_info *, struct conn_info *);
int	conn_chash_tbl_delete(struct conn_chash_tbl *, const struct conn_id *);
int	conn_chash_tbl_update(struct conn_chash_tbl *, const struct conn_id *,
	    int (*)(const struct conn_info *, struct conn_info *));
int	conn_chash_tbl_resize(struct conn_chash_tbl *, unsigned int);
void	conn_chash_tbl_destroy(struct conn_chash_tbl *);

/*
 * what we need to hash:
 *
 * - connection identifier: ((hostip, hostport), (clientip, clientport))
 *   need to be able to find connection data indicators fast
 *
 * - ip: to count number of current connections from one ip address (protection
 *   against ddos)
 *
 * - want keys inplace
 */

#endif

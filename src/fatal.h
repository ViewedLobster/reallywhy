#ifndef __FATAL_H
#define __FATAL_H

#define FATAL(msg) \
	do {									\
		fprintf(stderr, "%s:%d - FATAL ERROR: %s\n",			\
		    __FILE__, __LINE__, msg);					\
		abort();							\
	} while (0)

#define PRECOND(cond)								\
	do {									\
		if (!(cond)) {							\
			FATAL("failed PRECOND (" #cond ")");			\
		}								\
	} while (0)

#define ASSERT(cond)								\
	do {									\
		if (!(cond)) {							\
			FATAL("failed ASSERT (" #cond ")");			\
		}								\
	} while (0)

#define POSTCOND(cond)								\
	do {									\
		if (!(cond)) {							\
			FATAL("failed POSTCOND (" #cond ")");			\
		}								\
	} while (0)

// TODO make sure precond/assert/postcond are only active in debug build

#endif

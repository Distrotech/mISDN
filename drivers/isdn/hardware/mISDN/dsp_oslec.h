#ifdef __KERNEL__
#include <linux/kernel.h>       /* We're doing kernel work */
#include <linux/module.h>     
#include <linux/kernel.h>
#include <linux/slab.h>
#define malloc(a) kmalloc((a), GFP_KERNEL)
#define free(a) kfree(a)
#else
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#endif

#include "/usr/src/linux/drivers/staging/echo/oslec.h"

#define EC_TYPE "OSLEC"

struct echo_can_state {
	struct oslec_state	*oslec;
};

static inline struct echo_can_state *echo_can_create(int len, int adaption_mode) {
	struct echo_can_state *ec;
	ec = (struct echo_can_state *)malloc(sizeof(struct echo_can_state));
	ec->oslec = oslec_create(len, ECHO_CAN_USE_ADAPTION | ECHO_CAN_USE_NLP | ECHO_CAN_USE_CLIP | ECHO_CAN_USE_TX_HPF | ECHO_CAN_USE_RX_HPF);
	if (ec->oslec) {
		return ec;
	} else {
		free(ec);
		return NULL;
	}
}

static inline short echo_can_update(struct echo_can_state *ec, short iref, short isig) {
	return oslec_update(ec->oslec, iref, isig);
}

static inline int echo_can_traintap(struct echo_can_state *ec, int pos, short val) {
	return 0;
}

static inline void echo_can_free(struct echo_can_state *ec) {
	if (ec) {
		oslec_free(ec->oslec);
		free(ec);
	}
}

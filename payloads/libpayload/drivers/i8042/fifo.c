/*
 * This file is part of the libpayload project.
 *
 * Patrick Rudolph 2017 <siro@das-labor.org>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <libpayload-config.h>
#include <libpayload.h>
#include "fifo.h"

struct fifo {
	u8 *buf;
	u32 tx;
	u32 rx;
	u32 len;
};

/** Initialize a new fifo queue.
 * Initialize a new fifo with length @len.
 * @len: Length of new fifo
 * Returns NULL on error.
 */
struct fifo* fifo_init(u32 len)
{
	struct fifo* ret;

	ret = malloc(sizeof(*ret));
	if (!ret)
		return NULL;

	memset(ret, 0, sizeof(*ret));

	ret->buf = malloc(len);
	if (!ret->buf) {
		free(ret);
		return NULL;
	}

	ret->len = len;

	return ret;
}

/** Deletes a fifo queue.
 * @fifo: Fifo to delete
 */
void fifo_del(struct fifo* fifo)
{
	if (fifo) {
		if (fifo->buf)
			free(fifo->buf);
		free(fifo);
	}
}

/** Push object onto fifo queue.
 * Pushes a new object onto the fifo. In case the fifo
 * is full the oldest object is overwritten.
 * @fifo: Fifo to use
 * @c: Element to push
 */
void fifo_push(struct fifo* fifo, u8 c)
{
	fifo->buf[fifo->tx++] = c;
	fifo->tx = fifo->tx % fifo->len;
	if (fifo->tx == fifo->rx)
		fifo->rx ++;
	fifo->rx = fifo->rx % fifo->len;
}

/** Test fifo queue element count.
 * Returns 1 if fifo is empty.
 * @fifo: Fifo to use
 */
int fifo_is_empty(struct fifo* fifo)
{
	return fifo->tx == fifo->rx;
}

/** Pop element from fifo queue.
 * Returns the oldest object from queue if any.
 * In case the queue is empty 0 is returned.
 * @fifo: Fifo to use
 */
u8 fifo_pop(struct fifo* fifo)
{
	u8 ret;

	if (fifo_is_empty(fifo))
		return 0;

	ret = fifo->buf[fifo->rx++];
	fifo->rx = fifo->rx % fifo->len;

	return ret;
}

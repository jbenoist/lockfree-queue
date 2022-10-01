/*
 * Copyright (c) 1998-2010 Julien Benoist <julien@benoist.name>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY JULIEN BENOIST ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <stdlib.h>

#include "lfq.h"

typedef struct {
	void *ptr;
	unsigned long ref;
} element_t;

struct _lfq_t {
	size_t depth;
	__uint128_t *e;
	unsigned long rear;
	unsigned long front;
};

lfq_t lfq_create(size_t depth)
{
	lfq_t q = (lfq_t)malloc(sizeof(struct _lfq_t));
	if (q != NULL) {
		q->depth = depth;
		q->rear = q->front = 0;
		q->e = calloc(depth, sizeof(*q->e));
	}
	return q;
}

void lfq_free(lfq_t q)
{
  free(q->e);
  free(q);
}

int lfq_enqueue(volatile lfq_t q, void *data)
{
	__uint128_t _o, _n;
	unsigned long rear, front;
	element_t *old = (element_t *)&_o;
	element_t *new = (element_t *)&_n;
	for (;;) {
		rear = q->rear;
		_o = q->e[rear % q->depth];
		front = q->front;
		if (rear != q->rear) {
			continue;
		}
		if (rear == q->front + q->depth)  {
			element_t *cur = (element_t *)&q->e[front % q->depth];
			if (cur->ptr != NULL && front == q->front) {
				return 0;
			}
			__sync_bool_compare_and_swap(&q->front, front, front + 1);
			continue;
		}
		if (old->ptr == NULL) {
			new->ptr = data;
			new->ref = old->ref + 1;
			if (__sync_bool_compare_and_swap(&q->e[rear % q->depth], _o, _n)) {
				__sync_bool_compare_and_swap(&q->rear, rear, rear + 1);
				return 1;
			}
		} else {
			element_t *cur = (element_t *)&q->e[rear % q->depth];
			if (cur->ptr != NULL) {
				__sync_bool_compare_and_swap(&q->rear, rear, rear + 1);
			}
		}
	}
}

void *lfq_dequeue(volatile lfq_t q)
{
	__uint128_t _o, _n;
	unsigned long front, rear;
	element_t *old = (element_t *)&_o;
	element_t *new = (element_t *)&_n;
	for (;;) {
		front = q->front;
		_o = q->e[front % q->depth];
		rear = q->rear;
		if (front != q->front) {
			continue;
		}
		if (front == q->rear) {
			element_t *cur = (element_t *)&q->e[rear % q->depth];
			if (cur->ptr != NULL && rear == q->rear) {
				return NULL;
			}
			__sync_bool_compare_and_swap(&q->rear, rear, rear + 1);
			continue;
		}
		if (old->ptr != NULL) {
			new->ptr = NULL;
			new->ref = old->ref + 1;
			if (__sync_bool_compare_and_swap(&q->e[front % q->depth], _o, _n)) {
				__sync_bool_compare_and_swap(&q->front, front, front + 1);
				return old->ptr;
			}
		} else {
			element_t *cur = (element_t *)&q->e[front % q->depth];
			if (cur->ptr == NULL) {
				__sync_bool_compare_and_swap(&q->front, front, front + 1);
			}
		}
	}
}

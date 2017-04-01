# Fixed size lockfree queue (2010)

Queues are abstract data-types widely found in producer/consumer algorithms.
Many good implementations are lockfull and may be subject to high contention
with thousands of concurrent threads adding and consuming data on them.
The techniques described above can be used to implement an fully lockfree
array-based queue:
```
lockfree queue primitives
typedef struct _queue_t *queue_t;
queue_t queue_create(size_t);
void *queue_dequeue(queue_t);
int queue_enqueue(queue_t, void *);
```

The desired depth of the queue is defined at creation time by `queue_create`,
`queue_dequeue` returns the element address or `NULL` if the queue happens to
be empty, the address of the element to queue is passed to `queue_enqueue`
which returns `0` if the operation succeeded, and `1` if the queue is full.

Internal data-structures, initialization:
```
# file: lfq.h

31 #define E(val) ((element_t *)&val)
32
33 typedef struct {
34 	unsigned long ptr;
35 	unsigned long ref;
36 } element_t;
37
38 struct _queue_t {
39 	size_t depth;
40 	element_t *e;
41 	unsigned long rear;
42 	unsigned long front;
43 };
44
45 queue_t queue_create(size_t depth)
46 {
47 	queue_t q = (queue_t)malloc(sizeof(struct _queue_t));
48 	if (q) {
49 		q->depth = depth;
50 		q->rear = q->front = 0;
51 		q->e = (element_t *)calloc(depth, sizeof(element_t));
52 	}
53 	return q;
54 }
```

`element_t` is the double-word aligned structure that will hold the address of
elements enqueued and a count of updates to the slots.
`queue_t` contains the queue metadata with the capacity in depth, boundary
information in rear and front, and e is the contiguous storage for up to depth
elements.

The enqueue algorithm:
```
# file: lfq.c

e56 int queue_enqueue(volatile queue_t q, void *data)
e57 {
e58 	DWORD old, new;
e59 	unsigned long rear, front;
e60 	do {
e61 		rear = q->rear;
e62 		old = *((DWORD *)&(q->e[rear % q->depth]));
e63 		front = q->front;
e64 		if (rear != q->rear)
e65 			continue;
e66 		if (rear == (q->front + q->depth))  {
e67 			if (q->e[front % q->depth].ptr && front == q->front)
e68 				return 0;
e69 			CAS(&(q->front), front, front + 1);
e70 			continue;
e71 		}
e72 		if (!E(old)->ptr) {
e73 			E(new)->ptr = (uintptr_t)data;
e74 			E(new)->ref  = ((element_t *)&old)->ref + 1;
e75 			if (DWCAS((DWORD *)&(q->e[rear % q->depth]), old, new)) {
e76 				CAS(&(q->rear), rear, rear + 1);
e77 				return 1;
e78 			}
e79 		} else if (q->e[rear % q->depth].ptr)
e80 			CAS(&(q->rear), rear, rear + 1);
e81 	} while(1);
e82 }
```

The transaction starts at line `e61`. A snapshot of `q->rear` is taken,
followed by a full copy of the element at rear (line `e62`). At line `e64`, a
complimentary consistancy check of the snapshotted version of `rear` is done:
this is a non-mandatory optimization.  At line `e66`, the verification of
whether the queue is full is started - I'm saying "started" because the current
value of `q->front` is not enough to conclude that the queue is full: in fact,
a concurrent thread might just be about to successfully complete a dequeue at
line `d104`, in which case `q->front` would be lagging behind since the queue
now has one free slot. Thus, the two checks of line `e67` are used to make sure
the queue is full, in which case `0` is returned at line `e68`.

The code at line `e69` is a non-mandatory optimization in case `q->front` is
know as lagging behind: in an almost socialist manner, the thread simply helps
others upping `q->front` before jumping back to `e60` to retry the whole
transaction.  At line `e72`, another consistency check similar to the one of
`e67` assesses whether the slot's `ptr` field is non-null to ensure that a
concurrent thread which completed `e75` when the slot was snapshotted at `e62`
does not result in `q->rear` lagging behind, in which case the thread does an
extra consistancy check at line `e79` before helping increment `q->rear` at
line `e80` and jump back to `e60` to retry the transaction.

If the test of line `e72` succeeds, data is copied in the `128-bit` word in
stack memory (line `e73-e74`) and the `DWCAS` operation is attempted at line
`e75`. If it succeeds, the thread attempts to up `q->rear` and returns `1`; if
it fails, the whole set of operations is restarted at line `e60`.

You will notice that the status of all `CAS` operations of lines `e69`, `e76`
and `e80` are never verified. This is the expected behaviour as this algorithm
makes sure that any "lagging" of `q->rear` or `q->front` witnessed, due to
concurrent threads not done on `e76` or `d104` will attempt try to "help" by
doing a CAS in a lazy way.

the dequeue algorithm

```
# file: lfq.c

 d84 void *queue_dequeue(volatile queue_t q)
 d85 {
 d86 	DWORD old, new;
 d87 	unsigned long front, rear;
 d88 	do {
 d89 		front = q->front;
 d90 		old = *((DWORD *)&(q->e[front % q->depth]));
 d91 		rear = q->rear;
 d92 		if (front != q->front)
 d93 			continue;
 d94 		if (front == q->rear) {
 d95 			if (!q->e[rear % q->depth].ptr && rear == q->rear)
 d96 				return NULL;
 d97 			CAS(&(q->rear), rear, rear + 1);
 d98 			continue;
 d99 		}
d100 		if (E(old)->ptr) {
d101 			E(new)->ptr = 0;
d102 			E(new)->ref = E(old)->ref + 1;
d103 			if (DWCAS((DWORD *)&(q->e[front % q->depth]), old, new)) {
d104 				CAS(&(q->front), front, front + 1);
d105 				return (void *)((element_t *)&old)->ptr;
d106 			}
d107 		} else if (!q->e[front % q->depth].ptr)
d108 			CAS(&(q->front), front, front + 1);
d109 	} while(1);
d110 }
```

There is no need to explain the dequeue operation as it is exactly symetric to
the enqueue.

To verify the correctness of the queue implementation, we write a simple
program that spawns an even number of worker threads, with the first half of
those workers producing content and pushing it with `queue_enqueue`, and the
other half consumes it with `queue_dequeue`.

verification program
```
# file: test.c

34 static size_t iterations;
35 static volatile unsigned long input = 0;
36 static volatile unsigned long output = 0;
37
38 void *producer(void *arg)
39 {
40 	int i = 0;
41 	unsigned long *ptr;
42 	queue_t q = (queue_t)arg;
43 	for (i = 0; i < iterations; ++i) {
44 		ptr = (unsigned long *)malloc(sizeof(unsigned long));
45 		assert(ptr);
46 		*ptr = AAF(&input, 1);
47 		while (!queue_enqueue(q, (void *)ptr))
48 			;
49 	}
50 	return NULL;
51 }
52
53 void *consumer(void *arg)
54 {
55 	int i = 0;
56 	unsigned long *ptr;
57 	queue_t q = (queue_t)arg;
58 	for (i = 0; i < iterations; ++i) {
59 		while (!(ptr = (unsigned long *)queue_dequeue(q)))
60 			;
61 		AAF(&output, *ptr);
62 		*ptr = 0;
63 		free((void *)ptr);
64 	}
65 	return NULL;
66 }
67
68 int main(int argc, char **argv)
69 {
70 	int i;
71 	pthread_t *t;
72 	unsigned long verif = 0;
73 	queue_t q = queue_create(2);
74 	if (argc != 3) {
75 		fprintf(stderr, "%s: <nthreads> <iterations>\n", argv[0]);
76 		return 1;
77 	}
78 	if (atoi(argv[1]) % 2) {
79 		fprintf(stderr, "%s: need an even number of threads\n", argv[0]);
80 		return 1;
81 	}
82 	t = (pthread_t *)calloc(atoi(argv[1]), sizeof(pthread_t));
83 	iterations = atoi(argv[2]);
84 	for (i = 0; i < atoi(argv[1]); ++i)
85 		assert(!pthread_create(&(t[i]), NULL,
86 			(i % 2) ? consumer : producer, q));
87 	for (i = 0; i < atoi(argv[1]); ++i)
88 		assert(!pthread_join(t[i], NULL));
89 	for (i = 0; i <= input; ++i)
90 		verif += i;
91 	printf("input SUM[0..%lu]=%lu output=%lu\n", input, verif, output);
92 	return 0;
93 }
```

The content produced are numbers between between `1` and `iterations * threads / 2`
(line `44-46`).

Threads housekeep two shared counters:

Input line `34`, is upped by `1` each time an enqueue operation was completed
successfully (line `46`).  Output line `35`, is upped at line `61` by the value
of ptr that was just dequeued at line `59`. When all threads complete, the
main thread computes the sum of all the numbers between `1` and input to assert
the correct value of output.

Running the test on multi-core machine proves the implementation is correct:

```
$ ./lfq-test 64 1000000
input SUM[0..32000000]=512000016000000 output=512000016000000
```

Those examples have been verified in `64-bit` and `32-bit` mode on Linux and
Darwin (if you run the latter with an old version of gcc without support for
Atomic Builtins, you'll need to disable PIC with the `-mdynamic-no-pic` flag to
compile the inline assembly.)

References:

[1] Linux kernel's 'atomic' header for x86. kernel.org
https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/plain/arch/x86/include/asm/atomic.h?id=HEAD

[2] Intel(c) 64 and IA-32 Architecture Software Developer's Manual, Volume 3A: System Programming Guide Part 1. Intel
http://www.intel.com/content/www/us/en/architecture-and-technology/64-ia-32-architectures-software-developer-vol-3a-part-1-manual.html

[3] A practical nonblocking queue algorithm using compare-and-swap. Chien-Hua Shann; Ting-Lu Huang; Cheng Chen
http://ieeexplore.ieee.org/xpl/freeabs_all.jsp?arnumber=857731

[4] Formal Verification of an array-based nonblocking queue. Colvin, R; Groves, L
http://ieeexplore.ieee.org/xpl/freeabs_all.jsp?arnumber=1467933

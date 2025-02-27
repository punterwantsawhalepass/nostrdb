
#ifndef JB55_CURSOR_H
#define JB55_CURSOR_H

//#include <ctype.h>
//#include <assert.h>

#include <string.h>
#include <ctype.h>

#define unlikely(x) __builtin_expect((x),0)
#define likely(x)   __builtin_expect((x),1)

struct cursor {
	unsigned char *start;
	unsigned char *p;
	unsigned char *end;
};

static inline void make_cursor(unsigned char *start, unsigned char *end, struct cursor *cursor)
{
	cursor->start = start;
	cursor->p = start;
	cursor->end = end;
}

static inline int cursor_push_byte(struct cursor *cursor, unsigned char c)
{
	if (unlikely(cursor->p + 1 > cursor->end)) {
		return 0;
	}

	*cursor->p = c;
	cursor->p++;

	return 1;
}

static inline int cursor_push(struct cursor *cursor, unsigned char *data, int len)
{
	if (unlikely(cursor->p + len >= cursor->end)) {
		return 0;
	}

	if (cursor->p != data)
		memcpy(cursor->p, data, len);

	cursor->p += len;

	return 1;
}

static inline int cursor_push_str(struct cursor *cursor, const char *str)
{
	return cursor_push(cursor, (unsigned char*)str, (int)strlen(str));
}

static inline int cursor_push_c_str(struct cursor *cursor, const char *str)
{
	return cursor_push_str(cursor, str) && cursor_push_byte(cursor, 0);
}

static inline void *cursor_malloc(struct cursor *mem, unsigned long size)
{
	void *ret;

	if (mem->p + size > mem->end) {
		return 0;
	}

	ret = mem->p;
	mem->p += size;

	return ret;
}

static inline int cursor_slice(struct cursor *mem, struct cursor *slice, size_t size)
{
	unsigned char *p;
	if (!(p = cursor_malloc(mem, size))) {
		return 0;
	}
	make_cursor(p, mem->p, slice);
	return 1;
}

static inline size_t cursor_count(struct cursor *cursor, size_t elem_size) {
	return (cursor->p - cursor->start)/elem_size;
}

static inline int cursor_push_u32(struct cursor *cursor, uint32_t i) {
	return cursor_push(cursor, (unsigned char*)&i, sizeof(i));
}

static inline int cursor_push_u16(struct cursor *cursor, uint16_t i) {
	return cursor_push(cursor, (unsigned char*)&i, sizeof(i));
}

#define max(a,b) ((a) > (b) ? (a) : (b))
#include <stdio.h>
static inline void cursor_print_around(struct cursor *cur, int range)
{
	unsigned char *c;

	printf("[%ld/%ld]\n", cur->p - cur->start, cur->end - cur->start);

	c = max(cur->p - range, cur->start);
	for (; c < cur->end && c < (cur->p + range); c++) {
		printf("%02x", *c);
	}
	printf("\n");

	c = max(cur->p - range, cur->start);
	for (; c < cur->end && c < (cur->p + range); c++) {
		if (c == cur->p) {
			printf("^");
			continue;
		}
		printf("  ");
	}
	printf("\n");
}
#undef max

#endif

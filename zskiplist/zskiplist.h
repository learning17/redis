#include <stdio.h>
#include <stdlib.h>

#define ZSKIPLIST_MAXLEVEL 32
#define ZSKIPLIST_P 0.25

typedef struct {
	double min, max;
	int minex, maxex;
} zrangespec;

typedef struct zskiplistNode{
	char *obj;
	double score;
	struct zskiplistNode *backward;
	struct zskiplistLevel{
		struct zskiplistNode *forward;
		unsigned int span;
	} level[];
} zskiplistNode;

struct zskiplistLevel
{
	struct zskiplistNode *forward;
	unsigned int span;
};

typedef struct zskiplist{
	struct zskiplistNode *header, *tail;
	unsigned long length;
	int level;
} zskiplist;

zskiplist *zslCreate(void);
void zslFree(zskiplist *zsl);
zskiplistNode *zslInsert(zskiplist *zsl, double score, char *obj);
int zslDelete(zskiplist *zsl, double score, char *obj);
zskiplistNode *zslFirstInRange(zskiplist *zsl, zrangespec *range);
//zskiplistNode *zslLastInRange(zskiplist *zsl, zrangespec *range);
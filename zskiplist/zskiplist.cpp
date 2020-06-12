#include "zskiplist.h"
#include "zmalloc.h"
#include <string.h>

zskiplistNode *zslCreateNode(int level, double score, char *obj)
{
    zskiplistNode *zn = zmalloc(sizeof(*zn) + level*sizeof(struct zskiplistLevel));
    zn->score = score;
    zn->obj = obj;
}

void zslFreeNode(zskiplistNode *node)
{
    zfree(node);
}

int zslRandomLevel(void)
{
    int level = 1;
    while((random()&0xFFFF) < (ZSKIPLIST_P * 0xFFFF))
        level += 1;
    return (level < ZSKIPLIST_MAXLEVEL) ? level : ZSKIPLIST_MAXLEVEL;
}

void zslDeleteNode(zskiplist *zsl, zskiplistNode *x, zskiplistNode **update)
{
    int i;
    for(i = 0; i < zsl->level; i++){
        if(update[i]->level[i].forward == x){
            update[i]->level[i].span += x->level[i].span - 1;
            update[i]->level[i].forward = x->level[i].forward;
        } else{
            update[i]->level[i].span -= 1;
        }
    }
    if(x->level[0].forward){
        x->level[0].forward->backward = x->backward;
    } else {
        zsl->tail = x->backward;
    }

    while(zsl->level > 1 && zsl->header->level[zsl->level-1].forward == NULL)
        zsl->level--;
    zsl->length--;
}

static int zslValueGteMin(double value, zrangespec *spec)
{
    return spec->minex ? (value > spec->min) : (value >= spec->min);
}

static int zslValueLteMax(double value, zrangespec *spec)
{
    return spec->maxex ? (value < spec->max) : (value <= spec->max);
}

int zslIsInRange(zskiplist *zsl, zrangespec *range)
{   zskiplistNode *x;
    if(range->min > range->max || (range->min == range->max &&( range->minex || range->maxex)))
        return 0;
    x = zsl->tail;
    if(x == NULL || !zslValueGteMin(x->score, range))
        return 0;
    x = zsl->header->level[0].forward;
    if(x == NULL || !zslValueLteMax(x->score, range))
        return 0;
    return 1;
}

zskiplist *zslCreate(void)
{
    int j;
    zskiplist *zsl;
    zsl = zmalloc(sizeof(*zsl));
    zsl->level = 1;
    zsl->length = 0;
    zsl->header = zslCreateNode(ZSKIPLIST_MAXLEVEL, 0, NULL);
    for(j = 0; j < ZSKIPLIST_MAXLEVEL; j++){
        zsl->header->level[j].forward = NULL;
        zsl->header->level[j].span = 0;
    }
    zsl->header->backward = NULL;
    zsl->tail = NULL;
    return zsl;
}

void zslFree(zskiplist *zsl)
{
    zskiplistNode *node = zsl->header->level[0].forward, *next;
    zfree(zsl->header);
    while(node){
        next = node->level[0].forward;
        zslFreeNode(node);
        node = next;
    }
    zfree(zsl);
}

zskiplistNode *zslInsert(zskiplist *zsl, double score, char *obj)
{
    zskiplistNode *update[ZSKIPLIST_MAXLEVEL], *x;
    unsigned int rank[ZSKIPLIST_MAXLEVEL];
    int i, level;
    x = zsl->header;
    for(i = zsl->level-1; i >= 0; i--){
        rank[i] = i == zsl->level - 1 ? 0 : rank[i+1];
        while(x->level[i].forward && 
            (x->level[i].forward->score < score ||
                (x->level[i].forward->score == score && 
                    strcmp(x->level[i].forward->obj, obj)))){
            rank[i] += x->level[i].span;
            x = x->level[i].forward;
        }
        update[i] = x;
    }

    level = zslRandomLevel();
    if(level > zsl->level){
        for(i = zsl->level; i < level; i++){
            rank[i] = 0;
            update[i] = zsl->header;
            update[i]->level[i].span = zsl->length;
        }
        zsl->level = level;
    }

    x = zslCreateNode(level, score, obj);

    for(i = 0; i< level; i++){
        x->level[i].forward = update[i]->level[i].forward;
        update[i]->level[i].forward = x;
        x->level[i].span = update[i]->level[i].span - (rank[0] - rank[1]);
        update[i]->level[i].span = rank[0] - rank[i] + 1;
    }

    for(i = level; i < zsl->level; i++){
        update[i]->level[i].span++;
    }
    x->backward = (update[0] == zsl->header) ? NULL : update[0];
    if(x->level[0].forward)
        x->level[0].forward->backward = x;
    else
        zsl->tail = x;
    zsl->length++;
    return x;
}

int zslDelete(zskiplist *zsl, double score, char *obj)
{
    zskiplistNode *update[ZSKIPLIST_MAXLEVEL], *x;
    int i;
    x = zsl->header;
    for(i = zsl->level-1; i >= 0; i++){
        while(x->level[i].forward && 
            (x->level[i].forward->score < score ||
                (x->level[i].forward->score == score && 
                    strcmp(x->level[i].forward->obj, obj)))){
            x = x->level[i].forward;
        }
        update[i] = x;
    }
    x = x->level[0].forward;
    if(x && score == x->score && strcmp(x->obj, obj) == 0){
        zslDeleteNode(zsl, x, update);
        zslFreeNode(x);
        return 1;
    }
    return 0;
}

zskiplistNode *zslFirstInRange(zskiplist *zsl, zrangespec *range)
{
    zskiplistNode *x;
    int i;
    if(!zslIsInRange(zsl, range)) return NULL;
    x = zsl->header;
    for(i = zsl->level-1; i >= 0; i--){
        while(x->level[i].forward && !zslValueGteMin(x->level[i].forward->score, range))
            x = x->level[i].forward;
    }

    x = x->level[0].forward;
    if(!zslValueLteMax(x->score, range)) return NULL;
    return x;
}
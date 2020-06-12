/**
 * @file main.cpp
 * @brief 
 * @author ***
 * @version 1.0
 * @date 2020-06-11
 */
#include <stdio.h>
#include <stdlib.h>
#include "zmalloc.h"
#include "zskiplist.h"

int main()
{
  zskiplist *zsl = zslCreate();
  zslInsert(zsl, 0, "wang0");
  zslInsert(zsl, 1, "wang1");
  zslInsert(zsl, 2, "wang2");
  zslInsert(zsl, 3, "wang3");
  zslInsert(zsl, 4, "wang4");
  zslInsert(zsl, 5, "wang5");
  zslInsert(zsl, 6, "wang6");
  zslInsert(zsl, 7, "wang7");
  zslInsert(zsl, 8, "wang8");
  zslInsert(zsl, 9, "wang9");
  zslDelete(zsl, 2, "wang2");
  zrangespec *range = zmalloc(sizeof(*range));
  range->min = 1;
  range->max = 2;
  range->minex = 1;
  range->maxex = 0;
  zskiplistNode *node = zslFirstInRange(zsl, range);
  printf("obj:%s,score:%f\n",node->obj, node->score);
  return 0;
}


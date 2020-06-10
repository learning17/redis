/**
 * @file main.c
 * @brief 
 * @author ***
 * @version 1.0
 * @date 2020-05-30
 */
#include "sds.h"
#include "adlist.h"
#include <stdio.h>
#include <string.h>

int main()
{
  list *L = listCreate();
  L = listAddNodeHead(L,"wang");
  L = listAddNodeHead(L,"yu");
  L = listAddNodeHead(L,"long");
  L = listAddNodeTail(L, "wu");
  L = listAddNodeTail(L, "wan");
  L = listAddNodeTail(L, "li");
  list *L1 = listDup(L);
  listIter *iter = listGetIterator(L1, 0);
  listNode *node;
  while((node = listNext(iter)) != NULL){
    printf("%s\n", node->value);
  }
  return 0;
}

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "Structures.h"


void add(loc a, loc_list* l) {
    loc_list nve = malloc(sizeof(struct loc_list_s));
    nve->hd = a;
    nve->tl = *l;
    *l = nve;
}

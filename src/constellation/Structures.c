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

#if 0
Liste newListe(int nb_routeurs) {
    loc_list l = malloc(sizeof(struct loc_list_s));
    l->hd.nom = "Element nul";
    l->hd.coordonnees= malloc(sizeof(double) * nb_routeurs);
    l->hd.nombre_routeurs=0;
    l->tl=NULL;
    return(l);
}
#endif

#if 0
void efface_liste(Liste l)
{
    if (!est_vide(l))
    {
        efface_liste(l->tl);
    }
    free(l->hd.coordonnees);
    free(l);
}
#endif

#if 0
bool est_vide(loc_list l) {
	return l == NULL;
}
#endif

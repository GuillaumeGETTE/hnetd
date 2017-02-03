#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "Structures.h"


void add(loc a, Liste* l)
{
    Liste nve = (liste*) malloc(sizeof(struct liste));
    nve->hd = a;
    nve->tl = *l;
    *l = nve;
}


Liste newListe(int nb_routeurs)
{
    Liste l = (liste*)malloc(sizeof(struct liste));
    l->hd.nom = "Element nul";
    l->hd.coordonnees= malloc(sizeof(double) * nb_routeurs);
    l->hd.nombre_routeurs=0;
    l->tl=NULL;
    return(l);
}

void efface_liste(Liste l)
{
    if (!est_vide(l))
    {
        efface_liste(l->tl);
    }
    free(l->hd.coordonnees);
    free(l);
}

int est_vide(Liste l)
{
    return(l->tl==NULL);
}

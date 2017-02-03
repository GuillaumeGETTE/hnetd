#ifndef STRUCTURES_H_INCLUDED
#define STRUCTURES_H_INCLUDED

typedef struct loc loc;
struct loc
{
        double* coordonnees;
        int nombre_routeurs;
        char* nom;
};


typedef struct liste liste;
struct liste
{
    struct loc hd;
    struct liste* tl;
};

typedef struct liste* Liste;

void add(loc a,Liste* l);
//Ajoute l'élement a à la liste l

Liste newListe(int nb_routeurs);
//Crée une nouvelle liste vide

void efface_liste_vide(Liste l);
//Libère la mémoire de la liste vide l

int est_vide(Liste l);
//Vérifie si une liste est vide

#endif // STRUCTURES_H_INCLUDED

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
//Ajoute l'�lement a � la liste l

Liste newListe(int nb_routeurs);
//Cr�e une nouvelle liste vide

void efface_liste_vide(Liste l);
//Lib�re la m�moire de la liste vide l

int est_vide(Liste l);
//V�rifie si une liste est vide

#endif // STRUCTURES_H_INCLUDED

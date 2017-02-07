#ifndef STRUCTURES_H_INCLUDED
#define STRUCTURES_H_INCLUDED

typedef struct loc loc;
struct loc {
        double* coordonnees;
        int nombre_routeurs;
        char* nom;
};


typedef struct loc_list_s* loc_list;
struct loc_list_s {
    loc hd;
    loc_list tl;
};


/*
 * Ajoute l'�l�ment 'a' � la liste 'l'.
 * Les tableaux ne sont pas copi�s, seuls les pointeurs le sont.
 */
void add(loc a, loc_list* l);

#if 0
Liste newListe(int nb_routeurs);
//Cr�e une nouvelle liste vide

void efface_liste_vide(loc_list l);
//Lib�re la m�moire de la liste vide l
#endif

//int est_vide(loc_list l);
//V�rifie si une liste est vide

#endif // STRUCTURES_H_INCLUDED

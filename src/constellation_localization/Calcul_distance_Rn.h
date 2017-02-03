#ifndef CALCUL_DISTANCE_RN_H_INCLUDED
#define CALCUL_DISTANCE_RN_H_INCLUDED
#include "Structures.h"
#include <stdlib.h>
#include <stdio.h>

/*
 * Initalise les liste references, utilisateurs et liste_routeurs,
 * et renvoie le nombre de routeurs.
 */
int init(char* chemin_fichier, Liste* references, Liste* utilisateurs, char** liste_routeurs);

double distance(double a[],double b[],int n);
//Calcul des distances dans R^n ou a et b sont les tableaux des coordonn�es

void etallonage(char* nom_salle,double* coordonnees_salle, Liste* references, int nombre_routeurs);
//Ajoute � la liste des valeurs de r�f�rences (references) la mesure d'�tallonage de la salle (la mesure ajout�e est normalis�e).


void maj_utilisateur(char* nom_utilisateur, int routeur, double nouvelle_coordonnee, Liste* utilisateurs, int n);
//Met � jour la coordonn�e de l'utilisateur pour le routeur pass� en argument. Si ce dernier n'existe pas, il est cr�� et ajout� � la liste des utilisateurs.

char* salle(char*nom_utilisateur, Liste utilisateurs, Liste references);
//Donne la salle la plus proche de l'utilisateur avec la distance de R^n

double* normalizzzze(double* coordonnees, int n);
//Normalise le tableau de coordonn�es de R^n pour la norme 1

void maj_fichier_coordonnees(char* nom_du_fichier, Liste utilisateurs, Liste references);
//Remplace le contenu du fichier par la liste des utilisateurs et leurs coordonn�es.

#endif // CALCUL_DISTANCE_RN_H_INCLUDED

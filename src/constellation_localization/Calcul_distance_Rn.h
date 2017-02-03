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
//Calcul des distances dans R^n ou a et b sont les tableaux des coordonnées

void etallonage(char* nom_salle,double* coordonnees_salle, Liste* references, int nombre_routeurs);
//Ajoute à la liste des valeurs de références (references) la mesure d'étallonage de la salle (la mesure ajoutée est normalisée).


void maj_utilisateur(char* nom_utilisateur, int routeur, double nouvelle_coordonnee, Liste* utilisateurs, int n);
//Met à jour la coordonnée de l'utilisateur pour le routeur passé en argument. Si ce dernier n'existe pas, il est créé et ajouté à la liste des utilisateurs.

char* salle(char*nom_utilisateur, Liste utilisateurs, Liste references);
//Donne la salle la plus proche de l'utilisateur avec la distance de R^n

double* normalizzzze(double* coordonnees, int n);
//Normalise le tableau de coordonnées de R^n pour la norme 1

void maj_fichier_coordonnees(char* nom_du_fichier, Liste utilisateurs, Liste references);
//Remplace le contenu du fichier par la liste des utilisateurs et leurs coordonnées.

#endif // CALCUL_DISTANCE_RN_H_INCLUDED

#pragma once

#include "Structures.h"
#include <stdlib.h>
#include <stdio.h>

/*
 * Initalise les liste references, utilisateurs et liste_routeurs,
 * et renvoie le nombre de routeurs.
 */
int loc_init(char* chemin_fichier, loc_list* references, loc_list* utilisateurs, char** liste_routeurs);


/*
 * Ajoute � la liste des valeurs de r�f�rences (references) la mesure d'�tallonage de la salle (la mesure ajout�e est normalis�e).
 * 'nom_salle' n'est pas copi�, seul le pointeur l'est.
 */
void loc_etallonage(char* nom_salle, double* coordonnees_salle, loc_list* references, int nombre_routeurs);


/*
 * Met � jour la coordonn�e de l'utilisateur pour le routeur pass� en argument.
 * Si ce dernier n'existe pas, il est cr�� et ajout� � la liste des utilisateurs.
 * Le nom est copi�.
 */
void loc_maj_utilisateur(char* nom_utilisateur, int routeur, double nouvelle_coordonnee, loc_list* utilisateurs, int n);

/*
 * Renvoie la salle la plus proche de l'utilisateur avec la distance de R^n.
 * Renvoie NULL si aucune salle n'a �t� trouv�e, ou si l'utilisateur n'a pas �t� r�f�renc�.
 */
loc* loc_salle(char* nom_utilisateur, loc_list utilisateurs, loc_list references);


/*
 * Remplace le contenu du fichier par la liste des utilisateurs et leurs coordonn�es.
 */
void loc_maj_fichier_coordonnees(char* nom_du_fichier, loc_list utilisateurs, loc_list references);

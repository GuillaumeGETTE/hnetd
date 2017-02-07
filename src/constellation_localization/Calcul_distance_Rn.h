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
 * Ajoute à la liste des valeurs de références (references) la mesure d'étallonage de la salle (la mesure ajoutée est normalisée).
 * 'nom_salle' n'est pas copié, seul le pointeur l'est.
 */
void loc_etallonage(char* nom_salle, double* coordonnees_salle, loc_list* references, int nombre_routeurs);


/*
 * Met à jour la coordonnée de l'utilisateur pour le routeur passé en argument.
 * Si ce dernier n'existe pas, il est créé et ajouté à la liste des utilisateurs.
 * Le nom est copié.
 */
void loc_maj_utilisateur(char* nom_utilisateur, int routeur, double nouvelle_coordonnee, loc_list* utilisateurs, int n);

/*
 * Renvoie la salle la plus proche de l'utilisateur avec la distance de R^n.
 * Renvoie NULL si aucune salle n'a été trouvée, ou si l'utilisateur n'a pas été référencé.
 */
loc* loc_salle(char* nom_utilisateur, loc_list utilisateurs, loc_list references);


/*
 * Remplace le contenu du fichier par la liste des utilisateurs et leurs coordonnées.
 */
void loc_maj_fichier_coordonnees(char* nom_du_fichier, loc_list utilisateurs, loc_list references);

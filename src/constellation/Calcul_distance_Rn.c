#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "Calcul_distance_Rn.h"
#include "Structures.h"

/*
 * Calcul des distances dans R^n ou a et b sont les tableaux des coordonnées.
 */
static double distance(double a[], double b[], int n) {
	double r = 0;
	int i;
	for(i = 0 ; i < n ; i++)
		r += fabs(a[i] - b[i]);
	return r;
}

/*
 * Convertit des valeurs des dBm vers des milliWatts, puis les normalise selon
 * la norme 2. Prend les valeurs en dBm de 'dBm' et les convertit en puissance
 * normalisée dans 'power'. 'n' = tailles des tableaux en entrée.
 */
static void dBm_to_power_norm(double* dBm, double* power, int n) {
	int i;
	double norme, norme_carree = 0;

	for (i = 0 ; i < n ; ++i) {
		double pi = pow(10, dBm[i] / 10);
		power[i] = pi;
		norme_carree += pi * pi;
	}
	norme = sqrt(norme_carree);

	for (i = 0 ; i < n ; i++)
		power[i] /= norme;
}

int loc_init(const char* chemin_fichier, loc_list* references, loc_list* utilisateurs, char** liste_routeurs) {
	FILE* f;
	int nb_routeurs, nb_salles;
	char *routeurs;
	double* coordonnees;
	int k, i;
	char *nom_salle;

	f = fopen(chemin_fichier, "r");
	fscanf(f, "%d %d", &nb_routeurs, &nb_salles);

	/* On récupère les routeurs. */
	routeurs = malloc(nb_routeurs * 6); // 6 = taille d'une addresse mac
	*liste_routeurs = routeurs;
	for (k = 0 ; k < nb_routeurs ; ++k) {
		fscanf(f, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",
				routeurs + 0, routeurs + 1, routeurs + 2,
				routeurs + 3, routeurs + 4, routeurs + 5);
		routeurs += 6;
	}

	/* On récupère ensuite les informations des salles. */
	*references = NULL;
	coordonnees = malloc(nb_routeurs * sizeof(double));
	for (i = 0 ; i < nb_salles ; ++i) {
		nom_salle = malloc(145); // Pour mettre un tweet si besoin.
		fscanf(f, "%144s", nom_salle);
		for (k = 0 ; k < nb_routeurs ; ++k)
			fscanf(f, "%lf", coordonnees + k);
		loc_etallonage(nom_salle, coordonnees, references, nb_routeurs);
	}
	free(coordonnees);

	/* On initialise la liste des utilisateurs */
	*utilisateurs = NULL;

	fclose(f);
	return nb_routeurs;
}

void loc_etallonage(char* nom_salle, double* coordonnees_salle, loc_list* references, int nombre_routeurs) {
	loc a;
	a.nom = nom_salle;
	a.coordonnees = malloc(sizeof(double) * nombre_routeurs);
	dBm_to_power_norm(coordonnees_salle, a.coordonnees, nombre_routeurs);
	a.nombre_routeurs = nombre_routeurs;
	add(a, references);
}

void loc_maj_utilisateur(char* nom_utilisateur, int routeur, double nouvelle_coordonnee, loc_list* utilisateurs, int nb_routeurs) {
	if (*utilisateurs != NULL) {
		if (!strcmp(nom_utilisateur,(*utilisateurs)->hd.nom))
			/* L'utilisateur est présent dans la liste */
			(*utilisateurs)->hd.coordonnees[routeur] = nouvelle_coordonnee;
		else
			/* L'utilisateur n'a pas encore été trouvé */
			loc_maj_utilisateur(nom_utilisateur, routeur, nouvelle_coordonnee, &(*utilisateurs)->tl, nb_routeurs);
	} else {
		/* L'utilisateur n'est pas encore référencé, on l'ajoute */
		loc utilisateur;
		utilisateur.nom = malloc((strlen(nom_utilisateur) + 1) * sizeof(char));
		strcpy(utilisateur.nom, nom_utilisateur);
		utilisateur.nombre_routeurs = nb_routeurs;
		utilisateur.coordonnees = malloc(nb_routeurs * sizeof(double));
		int i;
		for (i = 0 ; i < nb_routeurs ; i++)
			utilisateur.coordonnees[i] = -10000.;
		utilisateur.coordonnees[routeur] = nouvelle_coordonnee;
		add(utilisateur, utilisateurs);
	}
}

loc* loc_salle(char* nom_utilisateur, loc_list utilisateurs, loc_list references) {
	/* On parcourt récursivement la liste des utilisateurs jusqu'à trouver celui que l'on veut localiser */
	if (!utilisateurs) {
		printf("Erreur : l'utilisateur n'est pas référencé\n");
		return NULL;
	} else if (strcmp(utilisateurs->hd.nom, nom_utilisateur))
		return loc_salle(nom_utilisateur, utilisateurs->tl, references);


	/* On cherche la salle référencée dont les coordonnées normalisées sont les
	 * plus proches de celles de l'utilisateur */
	loc utilisateur = utilisateurs->hd;
	int nb_routeurs = utilisateur.nombre_routeurs;
	double* coordonnees_normalisees = malloc(nb_routeurs * sizeof(double));
	dBm_to_power_norm(utilisateur.coordonnees, coordonnees_normalisees, nb_routeurs);

	loc* salle = NULL;
	double distance_salle = 3000000;

	while (references) {
		double dist = distance(coordonnees_normalisees, references->hd.coordonnees, utilisateur.nombre_routeurs);
		printf("%s : %f\n", references->hd.nom, dist);
		if (dist <= distance_salle) {
			salle = &references->hd;
			distance_salle = dist;
		}
		references = references->tl;
	}
	free(coordonnees_normalisees);
	return salle;
}

void loc_maj_fichier_coordonnees(char* nom_du_fichier, loc_list utilisateurs, loc_list references) {
	FILE* fichier = fopen(nom_du_fichier, "w+");
	loc_list utilisateurs_a_ecrire = utilisateurs;
	while (utilisateurs_a_ecrire) {
		char* nom_utilisateur = utilisateurs_a_ecrire->hd.nom;
		fputs(nom_utilisateur, fichier);
		fputs("    ", fichier);
		fputs(loc_salle(nom_utilisateur, utilisateurs, references)->nom, fichier);
		fputs("\n", fichier);
		utilisateurs_a_ecrire = utilisateurs_a_ecrire->tl;
	}
	fclose(fichier);
}

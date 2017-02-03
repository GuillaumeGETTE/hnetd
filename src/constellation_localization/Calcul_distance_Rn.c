#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "Calcul_distance_Rn.h"
#include "Structures.h"

int init(char* chemin_fichier, Liste* references, Liste* utilisateurs, char** liste_routeurs) {
	FILE* f = fopen(chemin_fichier, "r");
	int nb_routeurs, nb_salles;
	char *routeurs;
	double* coordonnees;

	fscanf(f, "%d %d", &nb_routeurs, &nb_salles);

	/* On récupère les routeurs. */
	routeurs = malloc(nb_routeurs * 6); // 6 = taille d'une addresse mac
	*liste_routeurs = routeurs;
	int k;
	for (k = 0 ; k < nb_routeurs ; ++k) {
		fscanf(f, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",
				routeurs + 0, routeurs + 1, routeurs + 2,
				routeurs + 3, routeurs + 4, routeurs + 5);
		routeurs += 6;
	}

	/* On récupère ensuite les informations des salles. */
	*references = newListe(nb_routeurs);
	coordonnees = malloc(nb_routeurs * sizeof(double));
	int i;
	for (i = 0 ; i < nb_salles ; ++i) {
		char nom_salle[145]; // Pour mettre un tweet si besoin.
		fscanf(f, "%144s", nom_salle);
		int k;
		for (k = 0 ; k < nb_routeurs ; ++k)
			fscanf(f, "%lf", coordonnees + k);
		etallonage(nom_salle, coordonnees, references, nb_routeurs);
	}

	/* On initialise la liste des utilisateurs */
	*utilisateurs = newListe(nb_routeurs);

	fclose(f);
	return nb_routeurs;
}

double distance(double a[],double b[],int n) {
	double r = 0;
	int i;
	for(i = 0 ; i < n ; i++) {
		r += fabs(a[i] - b[i]);
		printf("%.3f %.3f - ", a[i], b[i]);
	}
	return r;
}

double* normalizzzze(double* coordonnees, int n) {
	double* u = (double *) malloc(sizeof(double) * n);
	int i;
	double norme, norme_carree = 0;

	for (i = 0 ; i < n ; i++) {
		double ui = pow(10, coordonnees[i] / 10);
		u[i] = ui;
		norme_carree += ui * ui;
	}
	norme = sqrt(norme_carree);

	for (i = 0 ; i < n ; i++)
		u[i] /= norme;

	return u;
}

void etallonage(char* nom_salle,double* coordonnees_salle, Liste* references, int nombre_routeurs) {
	loc a;
	a.nom = malloc((strlen(nom_salle) + 1) * sizeof(char));
	strcpy(a.nom, nom_salle);
	double* t = normalizzzze(coordonnees_salle, nombre_routeurs);
	a.coordonnees = (double *) malloc(sizeof(double) * nombre_routeurs);
	int i;
	for (i = 0 ; i < nombre_routeurs ; i++)
		a.coordonnees[i]=t[i];
	a.nombre_routeurs = nombre_routeurs;
	add(a, references);
	free(t);
}

void maj_utilisateur(char* nom_utilisateur, int routeur, double nouvelle_coordonnee, Liste* utilisateurs, int n) {
	if (!est_vide(*utilisateurs)) {
		// L'utilisateur est présent dans la liste
		if (strcmp(nom_utilisateur,(*utilisateurs)->hd.nom))
			maj_utilisateur(nom_utilisateur, routeur, nouvelle_coordonnee, &(*utilisateurs)->tl, n);
		else
			(*utilisateurs)->hd.coordonnees[routeur] = nouvelle_coordonnee;
	} else {
		// L'utilisateur n'est pas encore référencé, on l'ajoute
		loc utilisateur;
		utilisateur.nom = malloc((strlen(nom_utilisateur) + 1) * sizeof(char));
		strcpy(utilisateur.nom, nom_utilisateur);
		utilisateur.nombre_routeurs = n;
		utilisateur.coordonnees = (double *) malloc(sizeof(double) * n);
		int i;
		for (i = 0 ; i < n ; i++)
			utilisateur.coordonnees[i] = -10000.;
		utilisateur.coordonnees[routeur] = nouvelle_coordonnee;
		add(utilisateur, utilisateurs);
	}
}


char* salle(char* nom_utilisateur, Liste utilisateurs, Liste references) {
	if (est_vide(utilisateurs)) {
		printf("Erreur : l'utilisateur n'est pas référencé\n");
		abort();
	}
	loc utilisateur = utilisateurs->hd;
	// On parcourt récursivement la liste des utilisateurs jusqu'à trouver celui que l'on veut localiser
	if (strcmp(utilisateur.nom, nom_utilisateur)) {
		return salle(nom_utilisateur, utilisateurs->tl, references);
	}
	double distance_salle = 3000000;
	// Les coordonnées seront normalisées, on met donc une distance initiale plus grande que celle que l'on pourrait obtenir (2) pour être sur qu'une salle sera sélectionnée.
	char* salle = "Pas de salle";
	double* coordonnees_normalisees = normalizzzze(utilisateur.coordonnees,utilisateur.nombre_routeurs);
	while (!est_vide(references)) {
		double dist = distance(coordonnees_normalisees, references->hd.coordonnees, utilisateur.nombre_routeurs);
		printf("%s : %f\n", references->hd.nom, dist);
		if (dist <= distance_salle) {
			distance_salle = dist;
			salle = references->hd.nom;
			//On compare les distances normalisées dans R^n et on garde la salle la plus proche
		}
		references = references->tl;
		//On fait la comparaison sur l'ensemble des salles
	}
	free(coordonnees_normalisees);
	return salle;
}



void maj_fichier_coordonnees(char* nom_du_fichier, Liste utilisateurs, Liste references)
{
	FILE* fichier = NULL;
	fichier = fopen(nom_du_fichier, "w+");
	Liste utilisateurs_a_ecrire=utilisateurs;
	while (!est_vide(utilisateurs_a_ecrire))
	{
		char* nom_utilisateur=utilisateurs_a_ecrire->hd.nom;
		fputs(nom_utilisateur,fichier);
		fputs("    ",fichier);
		fputs(salle(nom_utilisateur,utilisateurs,references),fichier);
		fputs("\n",fichier);
		utilisateurs_a_ecrire=utilisateurs_a_ecrire->tl;
	}
	fclose(fichier);
}

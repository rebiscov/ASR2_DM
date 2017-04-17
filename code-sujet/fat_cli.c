#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <stdlib.h>

#include "fat32_driver.h"
#include "utils.h"

/* Affiche comment se servir du programme. */
void print_help() {
    printf("Syntaxe : fat_cli <disque FAT> <commande> <arguments>\n");
    printf("Commandes acceptées :\n");
    printf("* tree: affiche un arbre des dossiers et fichiers\n");
    printf("* ls <chemin>: liste les fichiers dans un dossier\n");
    printf("* cat <chemin>: affiche le contenu d'un fichier\n");
    printf("\n");
    printf("Exemple :\n");
    printf("fat_cli chemin/vers/le/disque.img ls /chemin/vers/dossier/\n");
}

/* Vous pouvez éventuellement utiliser une fonction auxiliaire pour 'tree' */

void shift(unsigned int i){
  for (unsigned int j = 0; j < 2*i; j++)
    printf(" ");
}

void tree_aux(struct fat32_node *node, unsigned int prof){
  const char *name = fat32_node_get_name(node);

  if (!(strcmp(name, "..") == 0 || strcmp(name, ".") == 0)){
    if (fat32_node_is_dir(node)){
      shift(prof);
      printf("%s/\n", name);
      struct fat32_node_list* sub_d = fat32_node_get_children(node);
      struct fat32_node_list* parcours = sub_d;
      while (parcours != NULL){
	tree_aux(parcours->node, prof + 1);
	parcours = parcours->next;
      }
      fat32_node_list_free(sub_d);
    }
    else{
      shift(prof);
      printf("%s\n", name);
    }
  }
}

/* Gestion de la commande "tree". */
int tree(char* fat_disk) {
    struct fat32_driver *driver = fat32_driver_new(fat_disk);
    struct fat32_node *root = fat32_driver_get_root_dir(driver);

    tree_aux(root, 0);

    fat32_node_free(root);
    fat32_driver_free(driver);
    return EXIT_SUCCESS;
}

/* Gestion de la commande "ls". */
int ls(char* fat_disk, char* path) {
    struct fat32_driver *driver = fat32_driver_new(fat_disk);
    struct fat32_node *root = fat32_driver_get_root_dir(driver);
    struct fat32_node *folder = fat32_node_get_path(root, path);
    if (folder == NULL){
      printf("'%s' is not a valid path\n", path);
      return EXIT_SUCCESS;
    }

    struct fat32_node_list* content = fat32_node_get_children(folder);
    struct fat32_node_list* p = content;
    
    while (p != NULL){
      printf("%s", fat32_node_get_name(p->node));
      if (fat32_node_is_dir(p->node))
	  printf("/");
      printf("\n");
      p = p->next;
    }
    fat32_node_list_free(content);
    return EXIT_SUCCESS;
}

/* Gestion de la commande "cat". */
int cat(char* fat_disk, char* path) {
    struct fat32_driver *driver = fat32_driver_new(fat_disk);
    struct fat32_node *root = fat32_driver_get_root_dir(driver);

    assert(0); // TODO: remplacez-moi

}

int main(int argc, char* argv[]) {
    if (argc == 3 && strcmp(argv[2], "tree") == 0) {
        /* Si la commande est "tree" */
        return tree(argv[1]);
    }
    else if (argc == 4 && strcmp(argv[2], "ls") == 0) {
        /* Si la commande est "ls" */
        return ls(argv[1], argv[3]);
    }
    else if (argc == 4 && strcmp(argv[2], "cat") == 0) {
        /* Si la commande est "cat" */
        return cat(argv[1], argv[3]);
    }
    else {
        /* Pas une commande connue, on affiche comment se servir du
         * programme. */
        print_help();
        return EXIT_FAILURE;
    }

}

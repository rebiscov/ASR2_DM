#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "utils.h"
#include "fat32_driver.h"

#define LFN_ENTRY_SIZE 32
#define LFN_ENTRY_CHARACTERS 13
#define ENTRY_SIZE 32

struct fat32_node {
  const struct fat32_driver *driver;
  uint32_t first_cluster; /* Le numéro du premier cluster du nœud. */
  uint32_t offset; /* Le décalage dans le cluster. */
  char name[256*4]; /* Au plus 255 caractères de 4 octets chacun. */
  uint32_t nb_lfn_entries;
  bool is_root; /* Booléen indiquant si ce nœud est en fait le pseudo-nœud
		 * correspondant à la racine. */
};

struct fat32_driver {
  FILE *fd;

  /* Informations sur le disque venant du secteur de boot, cf
   * http://wiki.osdev.org/FAT */
  uint16_t bytes_per_sector; /* */
  uint8_t sectors_per_cluster;
  uint16_t nb_reserved_sectors;
  uint8_t nb_fats; /* Nombre de FATs (en général 2) */
  uint16_t nb_directories_at_root;
  uint32_t nb_sectors;
  uint32_t first_cluster_root;
  uint32_t sectors_per_fat;
};

void put_cursor(FILE* fd, unsigned int nb_bytes){
  fseek(fd, nb_bytes, SEEK_SET);
}

struct fat32_driver* fat32_driver_new(const char *image_name) {
  struct fat32_driver *driver = malloc(sizeof(struct fat32_driver));
  assert(driver); /* On s'assure que la mémoire a bien été allouée. */

  driver->fd = fopen(image_name, "r");
  
  put_cursor(driver->fd, 11);
  driver->bytes_per_sector = read_uint16_littleendian(driver->fd);
  driver->sectors_per_cluster = read_uint8(driver->fd);
  driver->nb_reserved_sectors = read_uint16_littleendian(driver->fd);
  driver->nb_fats = read_uint8(driver->fd);
  driver->nb_directories_at_root = read_uint16_littleendian(driver->fd);
  driver->nb_sectors = read_uint16_littleendian(driver->fd);
  if (driver->nb_sectors == 0) {
    put_cursor(driver->fd, 32);
    driver->nb_sectors = read_uint32_littleendian(driver->fd);
  }
  
  put_cursor(driver->fd, 44);
  driver->first_cluster_root = read_uint32_littleendian(driver->fd);
  put_cursor(driver->fd, 36);
  driver->sectors_per_fat = read_uint32_littleendian(driver->fd);

    

#ifdef DEBUG
  fprintf(stderr, "Bytes per sector: %d\n", driver->bytes_per_sector);
  fprintf(stderr, "Sectors per cluster: %d\n", driver->sectors_per_cluster);
  fprintf(stderr, "Number of reserved sectors: %d\n", driver->nb_reserved_sectors);
  fprintf(stderr, "Number of FATs: %d\n", driver->nb_fats);
  fprintf(stderr, "Number of sectors: %d\n", driver->nb_sectors);
  fprintf(stderr, "Index of first cluster of root dir: %d\n", driver->first_cluster_root);
  fprintf(stderr, "Sectors per FAT: %d\n", driver->sectors_per_fat);
#endif

  assert(driver->nb_directories_at_root == 0); /* Always 0 for FAT32. */

  return driver;
}

void fat32_driver_free(struct fat32_driver *driver) {
  assert(driver); /* On s'assure que ce n'est pas un pointeur NULL, ça ne devrait jamais arriver */
  fclose(driver->fd);
  free(driver);
  driver = NULL;
}

uint32_t next_cluster_index(const struct fat32_driver *driver, uint32_t cluster_index) { /* A partir de C renvoie C+1 */
  uint32_t position = (uint32_t)driver->bytes_per_sector * (uint32_t)driver->nb_reserved_sectors + cluster_index * 4;
  fseek(driver->fd, position, SEEK_SET);
  return read_uint32_littleendian(driver->fd);
}

uint32_t get_cluster_sector(const struct fat32_driver *driver, uint32_t cluster_index) { /* Renvoie le numéro du premier secteur d'un cluster donné  */
  return (uint32_t)driver->nb_reserved_sectors + driver->nb_fats * driver->sectors_per_fat + (uint32_t)driver->sectors_per_cluster * (cluster_index - 2 );
}

void read_in_cluster(const struct fat32_driver *driver, uint32_t cluster, uint32_t offset, size_t size, uint8_t *buf) { /* Lis size octets à partir de offset dans cluster et écrit le résultat dans buf */
  assert(offset+size <= driver->bytes_per_sector*driver->sectors_per_cluster);

  uint32_t sector = get_cluster_sector(driver, cluster);
  fseek(driver->fd, sector * driver->bytes_per_sector + offset, SEEK_SET);


  for (size_t i = size; i > 0; i --)
  buf[size - i] = read_uint8(driver->fd);

}

/* Lit une partie de l'entrée correspondant au nœud 'node'.
 * À partir de l'octet indiqué par 'offset', lit 'size' octets,
 * et les écrit dans le tampon 'buf'.
 *
 * Ne fonctionne pas pour le nœud racine. */
void read_node_entry(const struct fat32_node *node, uint32_t offset, size_t size, uint8_t *buf) {
  assert(!node->is_root);

  uint32_t cluster = node->first_cluster;
  uint32_t bytes_per_cluster = (uint32_t) node->driver->sectors_per_cluster*node->driver->bytes_per_sector;
  uint32_t current_offset = node->offset + offset;

  while (current_offset >= bytes_per_cluster){
    cluster = next_cluster_index(node->driver, cluster);
    current_offset -= bytes_per_cluster;
  }

  size_t bytes_to_read = size;

  while(bytes_to_read > 0){
    if (current_offset + bytes_to_read > bytes_per_cluster){
      read_in_cluster(node->driver, cluster, current_offset, bytes_per_cluster - current_offset, buf);
      current_offset = 0;
      bytes_to_read -= bytes_per_cluster - current_offset;
      buf += bytes_per_cluster - current_offset;
      cluster = next_cluster_index(node->driver, cluster);
    }
    else{
      read_in_cluster(node->driver, cluster, current_offset, bytes_to_read, buf);
      bytes_to_read = 0;
    }
  }


}

/* Lit le nom d'un nœud et les éventuelles entrées LFN (Long File Name)
 * et définit les champs 'name' et 'nb_lfn_entries' du nœud.
 *
 * Ne fonctionne pas pour le nœud racine. */
void read_name(struct fat32_node *node) {
  assert(!node->is_root);

  uint8_t attributes;
  read_node_entry(node, 11, 1, &attributes);

  if ((attributes & 0xf) == 0xf) { /* Long File Name */
    bool entries_found[20];
    memset(entries_found, false, 20*sizeof(bool));

    /* Un "long file name" a au plus 256 caractères, chacun codé sur
     * deux octets. */
    char utf16_name[256*2] = {0};

    /* On lit chacune des entrées LFN, qui est au format décrit ici :
     * http://wiki.osdev.org/index.php?title=FAT&oldid=19960#Long_File_Names */
    for (unsigned int i=0; i<64; i++) {
      uint8_t lfn_entry[32];
      read_node_entry(node, i*32, 32, (uint8_t*) &lfn_entry);

      /* Les six bits de poids faible du premier octet est l'indice
       * de cette entrée LFN */
      uint8_t entry_index = (uint8_t) ((lfn_entry[0] & 0x3f) - 1);

      entries_found[entry_index] = true;

      /* Puis, 5 caractères du nom (chaque caractère est sur deux octets). */
      memcpy(&utf16_name[(entry_index*LFN_ENTRY_CHARACTERS+0)*2], &lfn_entry[1], 5*2);

      /* Puis trois octets qu'on ignore */

      /* Puis 6 caractères du nom */
      memcpy(&utf16_name[(entry_index*LFN_ENTRY_CHARACTERS+5)*2], &lfn_entry[14], 6*2);

      /* Puis un octet qu'on ignore */

      /* Puis 2 caractères du nom */
      memcpy(&utf16_name[(entry_index*LFN_ENTRY_CHARACTERS+11)*2], &lfn_entry[28], 2*2);

      if (entry_index == 0) {
	/* C'est la dernière entrée LFN */
	node->nb_lfn_entries = i+1;

	/* Check there is no missing part. */
	for (uint8_t j=0; j<=i; j++) {
	  assert(entries_found[j]);
	}


	char *utf8_name = utf16_to_utf8(utf16_name, 256*2);
	strcpy(node->name, utf8_name);
	free(utf8_name);

	break;
      }
    }
  }
  else { /* Short file name */
    /*
    uint8_t buf[11];
    read_node_entry(node, 0, 11, buf);
    memcpy(node->name, buf, 8);
    memcpy(node->name + 9, buf + 8, 3);
    node->name[8] = '.';
    */
    uint8_t dump[11];
    read_node_entry(node, 0, 11, dump);
    size_t last_char = 7, next;
    while (last_char && dump[last_char] == ' ') last_char--;
    next = last_char + 1;
    memcpy(node->name, dump, next); 

    if (dump[8] != ' ') {
      node->name[next++] = '.';
      memcpy(node->name + next, dump + 8, 3);
      next += 3;
    }

    node->name[next] = '\0';

    node->nb_lfn_entries = 0;
  }
}

/* Récupère le nom d'un nœud.
 *
 * Ne fonctionne pas pour le nœud racine. */
const char* fat32_node_get_name(const struct fat32_node *node) {
  if (node->is_root) {
    return "<root>";
  }
  else {
    return node->name;
  }
}

/* Récupère l'octet d'attributs d'un nœud.
 *
 * Ne fonctionne pas pour le nœud racine. */
uint8_t fat32_node_get_attributes(const struct fat32_node *node) {
  uint8_t attributes;

  read_node_entry(node, 11+(node->nb_lfn_entries*LFN_ENTRY_SIZE), 1, &attributes);
    
  return attributes;
}

/* Renvoie true si et seulement si le nœud est un dossier. */
bool fat32_node_is_dir(const struct fat32_node *node) {
  if (node->is_root) {
    return true;
  }
  else {
    uint8_t attributes;
    attributes = fat32_node_get_attributes(node);
    return attributes == 0x10;
  }
}

struct fat32_node_list* read_dir_list(const struct fat32_driver *driver, uint32_t first_cluster) {
  uint32_t size_read = 0;
  struct fat32_node_list *list = NULL, *list_tmp;

  /* TODO: currently, this constructs the list in reverse order.
   * Make it in the right order? */
  while (true) {
    struct fat32_node *subdir = malloc(sizeof(struct fat32_node));
    assert(subdir); /* On s'assure que la mémoire a bien été allouée. */

    subdir->first_cluster = first_cluster;
    subdir->offset = size_read;
    subdir->driver = driver;
    subdir->is_root = false;

    uint8_t first_byte;
    read_node_entry(subdir, 0, 1, &first_byte);
    if (first_byte == 0xE5) {
      /* This entry is unused eg. deleted file. Skip to next entry. */
      memset(&subdir->name, 0, 12);
      read_node_entry(subdir, 1, 11, (uint8_t*) &subdir->name);
      fat32_node_free(subdir);
      size_read += ENTRY_SIZE;
      continue;
    }

    read_name(subdir);
    if (subdir->name[0] == '\0' && subdir->name[1] == '\0') {
      /* There is no more directory in this list. */
      fat32_node_free(subdir);
      return list;
    }

    list_tmp = list;
    list = malloc(sizeof(struct fat32_node_list));
    assert(list);
    list->node = subdir;
    list->next = list_tmp;

    size_read += ENTRY_SIZE + subdir->nb_lfn_entries*LFN_ENTRY_SIZE;
  }
}

struct fat32_node* fat32_driver_get_root_dir(const struct fat32_driver *driver) {
  struct fat32_node *root = malloc(sizeof(struct fat32_node));
  assert(root);
  root->driver = driver;
  root->is_root = true;
  return root;
}

uint32_t get_content_cluster(const struct fat32_node *node) {
  uint8_t bytes_strong[2];
  uint8_t bytes_weak[2];
  read_node_entry(node, 20 + node->nb_lfn_entries * LFN_ENTRY_SIZE, 2, bytes_strong);
  read_node_entry(node, 26 + node->nb_lfn_entries * LFN_ENTRY_SIZE, 2, bytes_weak);

  return (uint32_t) (bytes_weak[0] | bytes_weak[1] << 8 | bytes_strong[0] << 16 | bytes_strong[1] << 24);
}

struct fat32_node_list* fat32_node_get_children(const struct fat32_node *node) {
  if (node->is_root) {
    return read_dir_list(node->driver, node->driver->first_cluster_root);
  }
  else {
    assert(fat32_node_is_dir(node));
    uint32_t content_cluster = get_content_cluster(node);
    return read_dir_list(node->driver, content_cluster);
  }
}

char* get_next_name(const char *name){
  char *next_name = NULL;
  unsigned int offset, i = 0, n = (unsigned int)strlen(name);
  
  next_name = malloc(500*sizeof(char));
  
  for (offset = 0; name[offset] == '/'; offset++);
  
  while(name[offset] != '/' && offset < n){
    next_name[i] = name[offset];
    i++; offset++;
  }
  next_name[offset] = '\0';
  return next_name;
}

struct fat32_node* copy_node(const struct fat32_node *node){
  struct fat32_node *copy = NULL;
  copy = malloc(sizeof(struct fat32_node));
  assert(node);

  copy->driver = node->driver;
  copy->first_cluster = node->first_cluster;
  copy->offset = node->offset;
  strcpy(copy->name, node->name);
  copy->nb_lfn_entries = node->nb_lfn_entries;
  copy->is_root = node->is_root;
  
  return copy;
}

struct fat32_node* fat32_node_get_path(const struct fat32_node *node, const char *path) {
  const char *name = fat32_node_get_name(node), *name_aux = get_next_name(path);
  
  if (strcmp("", path) == 0){
    return copy_node(node);
  }
  else{
    struct fat32_node_list* list = fat32_node_get_children(node);
    while (strcmp(name_aux, fat32_node_get_name(list->node)) != 0 ){
      list = list->next;
      if (list == NULL)
	break;
    }
    
    if (list == NULL)
      return NULL;
    
    unsigned int n = (unsigned int)strlen(path);
    unsigned int offset;
    for (offset = 0; path[offset] == '/'; offset++);
    for (; path[offset] != '/' && offset < n; offset++);

    return fat32_node_get_path(list->node, path + offset);
  }
}

void fat32_node_read_to_fd(const struct fat32_node *node, FILE *fd) {
  uint32_t first_content_cluster = get_content_cluster(node);

  uint32_t content_size; assert(0); // TODO: remplacez-moi

  uint32_t buffer_size = (uint32_t) (node->driver->bytes_per_sector*node->driver->sectors_per_cluster);
  char buffer[buffer_size];

  uint32_t current_cluster = first_content_cluster;
  while (content_size) {
    assert(current_cluster != 0); /* Check we do not run out of the chain before we finish reading the file. */

    /* Compute the address of the first sector of the current cluster. */
    uint32_t sector = get_cluster_sector(node->driver, current_cluster);
    uint32_t sector_address = sector*node->driver->bytes_per_sector;

    uint32_t read_size = (content_size<buffer_size) ? content_size : buffer_size;

    fseek(node->driver->fd, sector_address, SEEK_SET);
    fread(buffer, read_size, 1, node->driver->fd);

    fwrite(buffer, read_size, 1, fd);

    current_cluster = next_cluster_index(node->driver, current_cluster);
    content_size = (uint32_t) (content_size - read_size);
  }
}

void fat32_node_free(struct fat32_node *node) {
  free(node);
}


void fat32_node_list_free(struct fat32_node_list *list) {
  struct fat32_node_list *list_tmp;
  while (list) {
    fat32_node_free(list->node);
    list_tmp = list->next;
    free(list);
    list = list_tmp;
  }
}

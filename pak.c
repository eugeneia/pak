/* pak: unpack, preview and create Quake 1 & 2 PAK files. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/dir.h>
#include <unistd.h>
#include <stdint.h>

#define VERSION "development"
#define LIST 0
#define UNPAK 1
#define PAK 2
#define SIGNATURE "PACK"
#define FILE_SEC_SIZE 64
#define FILENAME_LEN 56
#define FILEBUF_SIZE 10240
#define HEADERSIZE 12
#define FSIZET uint32_t
#define PAD "quakequakequakequakequakequakequakequakequakequakequake"

typedef struct file_list_struct {
  char *name;
  FSIZET len;
  struct file_list_struct *next;
} flist;

void usage();
void version();
int pak_init(FILE *);
int pak_walk(FILE *, int, int (*func)(FILE *, FSIZET, char *));
int file_list(FILE *, FSIZET, char *);
int file_extract(FILE *, FSIZET, char *);
FSIZET check_file(char *);
int cpyn_file(FILE *, FSIZET, FILE *);
flist *flist_mobj(char *);
void flist_add(flist *, flist **);
int rd_select(struct direct *);
int flist_rd(char *, flist **);
int pak_create(FILE *, flist *);

int main(int argc, char *argv[])
{
  int i, n, mode=PAK;
  FILE *pak;
  flist *files, *o;

  if (argc < 2) {
    usage();
    return 1;
  }
  if (*argv[1] == '-')
    switch (*++argv[1]) {
    case 'h':
      usage();
      return 0;
    case 'v':
      version();
      return 0;
    case 'l':
      mode = LIST;
      break;
    case 'x':
      mode = UNPAK;
      break;
    default:
      usage();
      return 1;
    }
  switch(mode) {
  case LIST:
  case UNPAK:
    for (i = 2; i < argc; i++) {
      if ((pak=fopen(argv[i], "r")) == NULL) {
	fprintf(stderr, "pak: Can not open %s.\n", argv[i]);
	return 1;
      }
      if ((n = pak_init(pak)) == 0) {
	fprintf(stderr, "pak: pak_init failed: pak corrupted.\n");
	fclose(pak);
	return 1;
      }
      printf("%s:\n", argv[i]);
      if (!pak_walk(pak, n, mode ? file_extract : file_list)) {
	fprintf(stderr, "pak: failed to process %s.\n", argv[i]);
	return 1;
      }
      fclose(pak);
    }
    return 0;
  case PAK:
    if ((pak=fopen(argv[1], "w")) == NULL) {
      fprintf(stderr, "pak: Can not open %s.\n", argv[i]);
      return 1;
    }
    files = NULL;
    for (n = 2; n < argc; n++) {
      if ((o = flist_mobj(argv[n])) != NULL)
	flist_add(o, &files);
      else if (!flist_rd(argv[n], &files))
	fprintf(stderr, "pak: Can not open %s.\n", argv[n]);
    }
    printf("%s:\n", argv[1]);
    if (!pak_create(pak, files)) {
      fprintf(stderr, "pak: Failed to create %s.\n", argv[1]);
      return 1;
    }
    fclose(pak);
    return 0;
  }
  return 0;
}

/* usage: prints usage information. */
void usage()
{
  puts("usage: pak [-hvlx] [PAK] [FILE...]");
  puts("  -h        print this usage information");
  puts("  -v        print version");
  puts("  -l        lists files in PAK");
  puts("  -x        extract files from PAK");
  puts("  PAK       Quake PAK file");
  puts("  FILE      arbitrary file");
  puts("Without an option (hvlx) selected pak will pack the files into PAK");
}

/* version: prints version information. */
void version()
{
  printf("pak %s\n", VERSION);
}

/* pak_init: check if given file is a pak, seek to directory section and
   return number of files in pak. */
int pak_init(FILE *pak)
{
  int i;
  FSIZET offset, len;
  char signature[] = SIGNATURE, buf[4];

  i = fread(buf, sizeof(char), 4, pak);
  if (ferror(pak) != 0 || i != 4)
    return 0;
  for (i = 0; i < 4; i++)
    if (signature[i] != buf[i])
      return 0;
  i = fread(&offset, sizeof(FSIZET), 1, pak);
  if (ferror(pak) != 0 || i != 1)
    return 0;
  i = fread(&len, sizeof(FSIZET), 1, pak);
  if (ferror(pak) != 0 || i != 1 || len % FILE_SEC_SIZE != 0)
    return 0;
  if (fseek(pak, offset, SEEK_SET) != 0)
    return 0;
  return (int) len / FILE_SEC_SIZE;
}

/* pak_walk: walk through the files of a pak and call func for each. */
int pak_walk(FILE *pak, int filecount,  int (*func)(FILE *pak, FSIZET len, char *name))
{
  int i, next;
  FSIZET pos, len;
  char name[FILENAME_LEN];

  for (i = 0; i < filecount; i++, fseek(pak, next, SEEK_SET)) {
    next = ftell(pak) + FILE_SEC_SIZE;
    fread(name, sizeof(char), FILENAME_LEN, pak);
    fread(&pos, sizeof(FSIZET), 1, pak);
    fread(&len, sizeof(FSIZET), 1, pak);
    if (ferror(pak) != 0 || feof(pak))
      return 0;
    fseek(pak, pos, SEEK_SET);
    if (!(*func)(pak, len, name))
      return 0;
  }
  return filecount;
}

/* file_list: list a file in conjunction with pak_walk. */
int file_list(FILE *pak, FSIZET len, char *name)
{
  printf("%s (%d bytes)\n", name, len);
  return 1;
}

/* file_extract: extract a file in conjunction with pak_walk. */
int file_extract(FILE *pak, FSIZET len, char *name)
{
  int i;
  char dir[FILENAME_LEN];
  FILE *ofp;

  for (i = 0; name[i] != '\0'; i++)
    if (name[i] == '/') {
      strncpy(dir, name, i);
      dir[i] = '\0';
      if (access(dir, 06))
	if (errno == ENOENT) {
	  printf("Creating %s\n", dir);
	  if (mkdir(dir, 0755)) {
	    fprintf(stderr, "pak: file_extract: can not create directory %s.\n", dir);
	    return 0;
	  }
	}
    }
  if ((ofp = fopen(name, "w")) == NULL) {
    fprintf(stderr, "pak: file_extract: can not write to %s.\n", name);
    return 0;
  }
  printf("Extracting %s (%d bytes)\n", name, len);
  i = cpyn_file(pak, len, ofp);
  fclose(ofp);
  return i;
}

/* check_file: return size of file if path is a readable file. */
FSIZET check_file(char *path)
{
  struct stat sbuf;

  if (stat(path, &sbuf) == -1)
    return 0;
  if (!S_ISREG(sbuf.st_mode))
    return 0;
  if (access(path, R_OK))
    return 0;
  return sbuf.st_size;
}

/* cpyn_file: copy n bytes from ifp to ofp. */
int cpyn_file(FILE *ifp, FSIZET n, FILE *ofp)
{
  FSIZET i;
  char buf[FILEBUF_SIZE];

  for (i = 0; n > 0; n = n - i) {
    i = (n < FILEBUF_SIZE) ? n : FILEBUF_SIZE;
    fread(buf, sizeof(char), i, ifp);
    if (ferror(ifp)) {
      fprintf(stderr, "pak: file_extract: read error.\n");
      return 0;
    }
    if (fwrite(buf, sizeof(char), i, ofp) < i) {
      fprintf(stderr, "pak: file_extract: write error.\n");
      return 0;
    }
  }
  return 1;
}

/* flist_mobj: create a flist object out of file and return a pointer to
   it. */
flist *flist_mobj(char *file)
{
  FSIZET len;
  flist *object;

  if (((len = check_file(file)) == 0) ||
      ((object = (flist *)malloc(sizeof(flist))) == NULL))
    return NULL;
  object->name = file;
  object->len = len;
  object->next = NULL;
  return object;
}

/* flist_add: add an flist object to an existing flist. */
void flist_add(flist *object, flist **list)
{
  flist *v;

  if (*list == NULL)
    *list = object;
  else {
    for (v = *list; v->next != NULL; v = v->next)
      ;
    v->next = object;
  }
}

/* rd_select: helper function for flist_rd's scandir. */
int rd_select(struct direct *entry)
{
  if ((strcmp(entry->d_name, ".") == 0) ||
      (strcmp(entry->d_name, "..") == 0))
    return 0;
  else
    return 1;
}

/* flist_rd: walk through dir recursively collecting readable files, add
   them to list. */
int flist_rd(char *dir, flist **list)
{
  int i, count;
  char *wd;
  flist *o;
  struct direct **files;

  count = scandir(dir, &files, rd_select, alphasort);
  if (count < 0)
    return 0;
  else if (count == 0)
    return 1;
  for (i = 0; i < count; i++) {
    if ((wd = (char *)malloc(strlen(files[i]->d_name)
			     + strlen(dir) + 2*sizeof(char))) == NULL) {
      fprintf(stderr, "pak: flist_rd: Skipping %s in %s, malloc failed.\n",
	      files[i]->d_name, dir);
      continue;
    }
    strcpy(wd, dir);
    strcat(wd, "/");
    strcat(wd, files[i]->d_name);
    if ((o = flist_mobj(wd)) != NULL)
      flist_add(o, list);
    else if (!flist_rd(wd, list)) {
      fprintf(stderr, "pak: flist_rd: Can not open %s.\n", wd);
      continue; 
    }
  }
  return 1;
}

/* pak_create: write a pak file consisting of files. */
int pak_create(FILE *pak, flist *files)
{
  int i;
  FSIZET doff, dlen, pos;
  FILE *f;
  flist *v;

  doff = HEADERSIZE, dlen = 0;
  for (v = files; v != NULL; v = v->next) {
    doff += v->len;
    dlen += FILE_SEC_SIZE;
  }
  fwrite(SIGNATURE, sizeof(char), 4, pak);
  fwrite(&doff, sizeof(FSIZET), 1, pak);
  fwrite(&dlen, sizeof(FSIZET), 1, pak);
  if (ferror(pak) != 0) {
    perror("pak: pak_create");
    return 0;
  }
  for (v = files; v != NULL; v = v->next) {
    printf("Packing %s (%d bytes)\n", v->name, v->len);
    if ((f = fopen(v->name, "r")) == NULL) {
      fprintf(stderr, "pak: pak_create: Can not open %s.\n", v->name);
      return 0;
    }
    i = cpyn_file(f, v->len, pak);
    fclose(f);
    if (!i) {
      fprintf(stderr, "pak: pak_create: Copy error in %s.\n", v->name);
      return 0;
    }
  }
  pos = 12;
  for (v = files; v != NULL; v = v->next) {
    i = strlen(v->name) + 1;
    fwrite((*(v->name) == '/' ? (v->name)+1 : v->name), sizeof(char), i, pak);
    fwrite(PAD, sizeof(char), FILENAME_LEN - i, pak);
    fwrite(&pos, sizeof(FSIZET), 1, pak);
    fwrite(&(v->len), sizeof(FSIZET), 1, pak);
    pos += v->len;
    if (ferror(pak) != 0) {
      perror("pak: pak_create");
      return 0;
    }
  }
  return 1;
}

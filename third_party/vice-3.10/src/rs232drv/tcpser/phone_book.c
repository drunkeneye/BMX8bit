#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include "phone_book.h"
#include "debug.h"

char phone_book[PH_BOOK_SIZE][2][PH_ENTRY_SIZE];
int size = 0;

int pb_init() {
  size = 0;
  return 0;
}

static char *pb_ltrim(char *txt) {
  while (txt != NULL && *txt != '\0' && isspace((unsigned char)*txt)) {
    ++txt;
  }
  return txt;
}

static void pb_rtrim(char *txt) {
  size_t len;

  if (txt == NULL) {
    return;
  }

  len = strlen(txt);
  while (len > 0 && isspace((unsigned char)txt[len - 1])) {
    txt[--len] = '\0';
  }
}

int pb_add(char* from, char* to) {
  LOG_ENTER();
  if(size < PH_BOOK_SIZE
     && from != NULL
     && to != NULL
     && strlen(from) > 0
     && strlen(to) > 0
    ) {
    // should really trim spaces.
    strncpy(phone_book[size][0], from, PH_ENTRY_SIZE - 1);
    phone_book[size][0][PH_ENTRY_SIZE - 1] = '\0';
    strncpy(phone_book[size][1], to, PH_ENTRY_SIZE - 1);
    phone_book[size][1][PH_ENTRY_SIZE - 1] = '\0';
    size++;
    LOG_EXIT();
    return 0;
  }
  LOG_EXIT();
  return -1;
}

int pb_search(char *number, char *address) {
  int i=0;

  LOG_ENTER();
  if (number == NULL) {
    address[0] = '\0';
    LOG_EXIT();
    return 0;
  } else {
    strncpy(address, number, PH_ENTRY_SIZE - 1);
    address[PH_ENTRY_SIZE - 1] = '\0';
  }
  for(i = 0; i < size; i++) {
    LOG(LOG_INFO, "Searching entry %d of %d", i, size);
    if(strcmp(phone_book[i][0], number) == 0) {
      LOG(LOG_INFO, "Found a match for '%s': '%s'", number, phone_book[i][1]);
      strncpy(address, phone_book[i][1], PH_ENTRY_SIZE - 1);
      address[PH_ENTRY_SIZE - 1] = '\0';
      break;
    }
  }
  LOG_EXIT();
  return 0;
}

int pb_load_file(const char *path) {
  FILE *fp;
  char line[PH_ENTRY_SIZE * 2 + 8];

  pb_init();
  if (path == NULL || path[0] == '\0') {
    return 0;
  }

  fp = fopen(path, "r");
  if (fp == NULL) {
    return -1;
  }

  while (fgets(line, sizeof line, fp) != NULL) {
    char *key;
    char *value;
    char *equals;

    key = pb_ltrim(line);
    if (key[0] == '\0' || key[0] == '#') {
      continue;
    }

    equals = strchr(key, '=');
    if (equals == NULL) {
      continue;
    }

    *equals = '\0';
    value = pb_ltrim(equals + 1);
    pb_rtrim(key);
    pb_rtrim(value);

    if (key[0] != '\0' && value[0] != '\0') {
      pb_add(key, value);
    }
  }

  fclose(fp);
  return 0;
}

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include "userlist.h"

void userlst_create(struct userlst_t * ulist, int max){
  ulist->max_same_user = max;
  ulist->head = NULL;
}

struct userlst * new_userlst(char * username){
  struct userlst * novo = (struct userlst *)malloc(sizeof(struct userlst));
  if (novo == NULL){
    puts("Erro de alocação de recurso");
    exit(255);
  }
  strcpy(novo->username, username);
  novo->how_many = 1;
  novo->next = NULL;
}

int userlst_insert(struct userlst_t * ulist, char * username){
  struct userlst * actual = ulist->head;
  struct userlst * before = NULL;

  // Se encontra username, tenta adicionar 1
  while(actual != NULL){
    if (strcmp(actual->username, username) == 0){
      if (actual->how_many >= ulist->max_same_user){
        return -1;
      }else{
        actual->how_many++;
        return actual->how_many;
      }
    }
    before = actual;
    actual = actual->next;
  }

  // Senão, cria nova entrada
  struct userlst * nova_entrada = new_userlst(username);
  if(actual == before){
    ulist->head = nova_entrada;
  }else{
    before->next = nova_entrada;
  }
  return nova_entrada->how_many;
}

void userlst_remove(struct userlst_t * ulist, char * username){
  struct userlst * actual = ulist->head;
  struct userlst * before = NULL;

  while(actual != NULL){
    if (strcmp(actual->username, username) == 0){
      if (actual->how_many > 0){
        actual->how_many--;
        // puts("Diminuido");
        return;
      }else{
        // Temos que remover o actual
        if (before == NULL){
          ulist->head = actual->next;
        }else{
          before->next = actual->next;
        }
        free(actual);
        // puts("Removido");
        return;
      }
    }
    before = actual;
    actual = actual->next;
  }
}

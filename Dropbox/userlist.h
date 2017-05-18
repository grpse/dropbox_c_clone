/*
O seguinte arquivo define uma estrutura de lista para um determinado usuário
Será utilizado pelo servidor para identificar quantas conexões de determinando
usuário existem ativas
*/

#include "util.h"

struct userlst {
  char username[USERNAME_MAX];
  int how_many;
  struct userlst * next;
};


struct userlst_t{
  int max_same_user;
  struct userlst * head;
};

// Inicializa a lista, cabeça
void userlst_create(struct userlst_t * ulist, int max);

// Insere usuário ou atualiza o how_many
// retorna -1 em caso de ter se chegado ao número máximo
int userlst_insert(struct userlst_t * ulist, char * username);

// Remove ou diminui 1 do how_many
void userlst_remove(struct userlst_t * ulist, char * username);

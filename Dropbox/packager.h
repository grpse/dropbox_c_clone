/*
As seguintes funções têm o intuito de prover os cabeçalhos das mensagens trocadas entre cliente e servidor
Cada mensagem terá um identificador de 1 byte, e os dados a seguir..

--------------------------------------------------------------------------------
Mensagens trocadas:

HI: Iniciar conexão
0x01,
tamanho_username (+1 pelo \0) - 4bytes,
username - tamanho_username bytes

RESPONSE: Respostas do servidor
0x02,
ok | erro - 1byte,
tamanho_msg - 4bytes,
msg - tamanho_msg bytes

UPDATED: Cliente pergunta se arquivo está atualizado
0x03,
tamanho_nome - 4bytes,
nome - tamanho_nome bytes,
data_modif - 4bytes? (tamanho da estrutura de tempo)

LS: Pedido do cliente por lista de arquivos
0x04

LIST: Resposta do Servidor com lista de arquivos
0x05,
nro_arquivos - 4bytes,
[
	tamanho_nome - 4bytes,
	nome - tamanho_nome bytes,
	data_modif
]

GET: Pedido do cliente por arquivo
0x06,
tamanho_nome - 4bytes,
nome - tamanho_nome bytes

UPLOAD: Pedido do cliente para começar a enviar um arquivo
0x07,
tamanho_nome - 4bytes,
nome - tamanho_nome bytes

FILE: Envia arquivo
0x08
tamanho_nome - 4bytes,
nome - tamanho_nome bytes,
data_modif,
tamanho_dados - 4bytes,
dados - tamanho_dados bytes


DELETE: Deleta determinado arquivo
0x09,
tamanho_nome - 4bytes,
nome - tamanho_nome bytes

CLOSE: Fecha conexão com servidor
0x0A

--------------------------------------------------------------------------------

Para cada função, a conversa funcionará da seguinte maneira:

CLIENTE				|			SERVIDOR

---- connect_server ----
HI						|
							|			RESPONSE


---- sync_client ----
Para cada arquivo:
UPDATED				|
							|			RESPONSE
Se precisa update:
							|			FILE

---- send_file ----
UPLOAD				|
							|			RESPONSE
Se aceitou upload:
FILE					|
							|			RESPONSE


---- get_file ----
GET						|
							|			RESPONSE
Se existe arquivo, tudo certo:
							|			FILE


---- list_files ----
LS						|
							|			LIST


*** Deletar não diz, mas seria interessante ***
---- delete_file ----
DELETE				|
							|			RESPONSE

---- close_connection ----
CLOSE					|
							|			RESPONSE

*/


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include "dropboxUtil.h"


const char * MES_HI;
const char * MES_RESPONSE;
const char * MES_UPDATED;
const char * MES_LS;
const char * MES_LIST;
const char * MES_GET;
const char * MES_UPLOAD;
const char * MES_FILE;
const char * MES_DELETE;
const char * MES_CLOSE;
// -----------------------------------------------------------------------------
// Funções para uso de string
//void package_message(char * command, char * message, char * buffer);

void package_hi(char * username, char * buffer);

void package_response(int response, char * msg, char * buffer);

// Com data fica pra depois
//void package_updated()
void package_updated(char * filename, char * mtime, char * buffer);

void package_ls(char * buffer);

// Com data fica pra depois
void package_list(char * dir_info, char * buffer);

void package_get(char * filename, char * buffer);

void package_upload(char * filename, char * buffer);

// Com data fica para depois
// Exige que seja enviado o arquivo em separado
void package_file(char * filename, char * mtime, uint32_t fsize, char * buffer);

void package_delete(char * filename, char * buffer);

void package_close(char * buffer);

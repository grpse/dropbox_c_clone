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
#include <time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include "packager.h"

const char * MES_HI =  "HI";
const char * MES_RESPONSE =  "RES";
const char * MES_UPDATED =  "UPD";
const char * MES_LS =  "LS";
const char * MES_LIST =  "LST";
const char * MES_GET =  "GET";
const char * MES_UPLOAD =  "UPL";
const char * MES_FILE =  "FIL";
const char * MES_DELETE =  "DEL";
const char * MES_CLOSE =  "CLS";

// Escreve em buffer o tamanho da string, e depois a string com o \0
// Para caso de bytes
void package_string(char * str, char * buffer){
	uint32_t size_str = strlen(str) + 1;
	memcpy((void *)buffer, (void *)&size_str, 4);
	strcpy(&buffer[4], str);
}



// -----------------------------------------------------------------------------
// Funções para uso de string
void package_message(const char * command, char * message, char * buffer){
	sprintf(buffer, "%s %s", command, message);
}


// Vai no Util
#define USERNAME_MAX 64
#define MESSAGE_MAX 64

void package_hi(char * username, char * buffer){
	package_message(MES_HI, username, buffer);
}

void package_response(int response, char * msg, char * buffer){
	char message[MESSAGE_MAX + 20];
	sprintf(message, "%d %s", response, msg);
	package_message(MES_RESPONSE, message, buffer);
}

void package_updated(char * filename, __time_t mtime, char * buffer){
	char message[MESSAGE_MAX + 20];
	sprintf(message, "%s %ld", filename, mtime);
	package_message(MES_UPDATED, message, buffer);
}

void package_ls(char * buffer){
	package_message(MES_LS, "", buffer);
}

// Com data fica pra depois
// Definir acesso ao diretório
void package_list(char * dir_info, char * buffer){
	package_message(MES_LIST, dir_info, buffer);
}

void package_get(char * filename, char * buffer){
	package_message(MES_GET, filename, buffer);
}

void package_upload(char * filename, char * buffer){
	package_message(MES_UPLOAD, filename, buffer);
}

// Com data fica para depois
// Definir como funcionará acesso ao arquivo
void package_file(char * filename, __time_t mtime, uint32_t fsize, char * buffer){
	char message[128];
	sprintf(message, "%s %ld %d", filename, mtime, fsize);
	package_message(MES_FILE, message, buffer);
}

void package_delete(char * filename, char * buffer){
	package_message(MES_DELETE, filename, buffer);
}

void package_close(char * buffer){
	package_message(MES_CLOSE, "", buffer);
}

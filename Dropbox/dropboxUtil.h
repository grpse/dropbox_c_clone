
#define USERNAME_MAX 64
#define MESSAGE_MAX 64
#define MAX_USERID 64
#define MAXFILES 256

#define MAX_SAME_USER 2

#define PORT 9000
#define BUF_SIZE 2048


int read_until_eos(int sock, char * buffer);
int read_n_from_socket(int n, int sock, char *buffer);
int write_str_to_socket(int sock, char * str);

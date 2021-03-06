#include <stdio.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <errno.h>
#include <netdb.h>
#include <string.h>
#include <strings.h>
#include "libnetfiles.h"

// this struct is to format signals from and to the client
typedef struct pack {
  char file_name[128];
  int access_mode;
  int op_code;
  int fp;
  int size;
} pack;

int init = -1;
int socket_descriptor = -1;
int g_filemode = -1; //should not be set unless netserverinit is called. G for global.
void *send_buffer;
void *receive_buffer;
struct sockaddr_in remote_server;
struct hostent *remote_ip = NULL;

//Connet to remote server specified by hostname.
int netserverinit(char * hostname, int filemode) {
  remote_ip = gethostbyname(hostname);

  //Host not found.
  if (remote_ip == NULL) {
    h_errno = HOST_NOT_FOUND;
    return -1;
  }

  socket_descriptor = socket(AF_INET, SOCK_STREAM, 0);

  remote_server.sin_family = AF_INET;
  bcopy((char *)remote_ip->h_addr, (char *)&remote_server.sin_addr.s_addr, remote_ip->h_length);
  remote_server.sin_port = htons(9999);

  //If connection failed, report IO error.
  if (connect (socket_descriptor, (struct sockaddr *)&remote_server, sizeof(remote_server)) < 0) {
    fprintf(stderr, "ERROR: could not connect to remote server.\n");
    errno = EIO;
    return -1;
  }

  send_buffer = malloc(sizeof(pack));
  pack *client_message = send_buffer;
  
  client_message->op_code = OP_INIT;
  client_message->access_mode = filemode;
  
  write(socket_descriptor, client_message, sizeof(pack));

  free(send_buffer);
  send_buffer = NULL;

  g_filemode = filemode;
  init = 1;

  close(socket_descriptor);

  return 0;
}

//Open remote file @<host>:<pathname>
//Flags are R/W/RW
int netopen(const char * pathname, int flags) {
  //If no valid socket descriptor, there is no valid connection.
  if (init == -1) {
    h_errno = HOST_NOT_FOUND;
    return -1;
  }

  socket_descriptor = socket(AF_INET, SOCK_STREAM, 0);

  //If connection failed, report IO error.
  if (connect (socket_descriptor, (struct sockaddr *)&remote_server, sizeof(remote_server)) < 0) {
    fprintf(stderr, "ERROR: could not connect to remote server: %s.\n", strerror(errno));
    errno = EIO;
    return -1;
  }

  send_buffer = malloc(sizeof(pack));
  receive_buffer = malloc(sizeof(pack));

  pack *client_message = send_buffer;
  pack *server_message = receive_buffer;
  
  strcpy(client_message->file_name, pathname);
  client_message->access_mode = flags;
  client_message->op_code = OP_OPEN;

  write(socket_descriptor, client_message, sizeof(pack));
  read(socket_descriptor, server_message, sizeof(pack));

  if (server_message->size == -1) {
    errno = server_message->op_code;
    return -1;
  }

  free(send_buffer);
  send_buffer = NULL;
  free(receive_buffer);
  receive_buffer = NULL;

  close(socket_descriptor);

  return server_message->size;
}


//Read nbyte bytes into buf from remote fd
ssize_t netread(int fd, void * buf, size_t nbyte) {
  //No socket descriptor == no connection
  if (init == -1) {
    h_errno = HOST_NOT_FOUND;
    return -1;
  }

  socket_descriptor = socket(AF_INET, SOCK_STREAM, 0);

  //If connection failed, report IO error.
  if (connect (socket_descriptor, (struct sockaddr *)&remote_server, sizeof(remote_server)) < 0) {
    fprintf(stderr, "ERROR: could not connect to remote server: %s.\n", strerror(errno));
    errno = EIO;
    return -1;
  }

  send_buffer = malloc(sizeof(pack));
  receive_buffer = malloc(sizeof(pack));

  pack *client_message = send_buffer;
  pack *server_message = receive_buffer;

  client_message->op_code = OP_READ;
  client_message->fp = fd;
  client_message->size = nbyte;

  write(socket_descriptor, client_message, sizeof(pack));
  read(socket_descriptor, server_message, sizeof(pack));

  if (server_message->size == -1) {
    errno = server_message->op_code;
	return -1;
  } else if (server_message->size > nbyte) {
    errno = EREMOTEIO;
    return -1;
  }

  read(socket_descriptor, buf, server_message->size);

  free(send_buffer);
  send_buffer = NULL;
  free(receive_buffer);
  receive_buffer = NULL;

  close(socket_descriptor);

  return server_message->size;
}


//Write nbyte bytes from buf into remote fd
ssize_t netwrite(int fd, const void * buf, size_t nbyte) {
  //No socket no conn
  if (init == -1) {
    h_errno = HOST_NOT_FOUND;
    return -1;
  }

  socket_descriptor = socket(AF_INET, SOCK_STREAM, 0);

  //If connection failed, report IO error.
  if (connect (socket_descriptor, (struct sockaddr *)&remote_server, sizeof(remote_server)) < 0) {
    fprintf(stderr, "ERROR: could not connect to remote server: %s.\n", strerror(errno));
    errno = EIO;
    return -1;
  }

  send_buffer = malloc(sizeof(pack));
  receive_buffer = malloc(sizeof(pack));

  pack *client_message = send_buffer;
  pack *server_message = receive_buffer;

  client_message->op_code = OP_WRITE;
  client_message->fp = fd;
  client_message->size = nbyte;

  write(socket_descriptor, client_message, sizeof(pack));
  write(socket_descriptor, buf, nbyte);
  read(socket_descriptor, server_message, sizeof(pack));

  if (server_message->size == -1) {
    errno = server_message->op_code;
	return -1;
  } else if (server_message->size > nbyte) {
    errno = EREMOTEIO;
	return -1;
  }

  free(send_buffer);
  send_buffer = NULL;
  free(receive_buffer);
  receive_buffer = NULL;

  close(socket_descriptor);

  return server_message->size;
}


int netclose(int fd) {
  //You get the drill by this point
  if (init == -1) {
    h_errno = HOST_NOT_FOUND;
    return -1;
  }

  socket_descriptor = socket(AF_INET, SOCK_STREAM, 0);

  //If connection failed, report IO error.
  if (connect (socket_descriptor, (struct sockaddr *)&remote_server, sizeof(remote_server)) < 0) {
    fprintf(stderr, "ERROR: could not connect to remote server: %s.\n", strerror(errno));
    errno = EIO;
    return -1;
  }

  send_buffer = malloc(sizeof(pack));
  receive_buffer = malloc(sizeof(pack));

  pack *client_message = send_buffer;
  pack *server_message = receive_buffer;

  client_message->op_code = OP_CLOSE;
  client_message->fp = fd;
  
  write(socket_descriptor, client_message, sizeof(pack));
  read(socket_descriptor, server_message, sizeof(pack));

  if (server_message->size == -1) {
    errno = server_message->op_code;
    return -1;
  }

  free(send_buffer);
  send_buffer = NULL;
  free(receive_buffer);
  receive_buffer = NULL;

  close(socket_descriptor);

  return 0;
}

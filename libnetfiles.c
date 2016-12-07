#include <stdio.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <errno.h>
//#include <netdb.h> //Supposedly defines "HOST_NOT_FOUND"

// this struct is to format signals from and to the client
typedef struct pack {
  char file_name[128];
  int access_mode;
  int op_code;
  int fp;
  int size;
} pack;

int socket_descriptor = -1;
int curr_fildes = 0;
void *send_buffer[sizeof(pack)];
void *receive_buffer[sizeof(pack)];
struct sockkaddr_in remote_server = NULL;
struct hostent *remote_ip = NULL;

//Connet to remote server specified by hostname.
int netserverinit(char * hostname) {
  remote_ip = gethostbyname(hostname);

  //Host not found.
  if (remote_ip == NULL) {
    h_errno = HOST_NOT_FOUND;
    return -1;
  }

  socket_descriptor = socket(AF_INET, SOCK_STREAM, 0);

  remoteServer.sin_family = AF_INET;
  bcopy((char *)remote_ip->h_addr, (char *)remoteServer.sin_addr.s_addr, remote_ip->h_length);
  remoteServer.sin_port = htons(9999);

  //If connection failed, report IO error.
  if (connect (socket_descriptor, (struct sockaddr *)&remote_server, sizeof(remote_server)) < 0) {
    fprintf(stderr, "ERROR: could not connect to remote server.\n");
    errno = EIO;
    return -1;
  }

  return 0;
}

//Open remote file @<host>:<pathname>
//Flags are R/W/RW
int netopen(const char * pathname, int flags) {
  //If no valid socket descriptor, there is no valid connection.
  if (socket_descriptor == -1) {
    h_errno = HOST_NOT_FOUND;
    return -1;
  }

  pack *client_message = send_buffer;
  pack *server_message = receive_buffer;
  
  client_message->file_name = pathname;
  client_message->access_mode = flags;
  client_message->op_code = 0;

  write(socket_descriptor, client_message, sizeof(pack));
  read(socket_descriptor, server_message, sizeof(pack));

  if (server_message->size == -1) {
    errno = server_message->op_code;
    return -1;
  }

  curr_fildes = server_message->size;
  return 0;
}


//Read nbyte bytes into buf from remote fd
size_t netread(int fd, const void * buf, size_t nbyte) {
  //No socket descriptor == no connection
  if (socket_descriptor == -1) {
    h_errno = HOST_NOT_FOUND;
    return -1;
  }

  pack *client_message = send_buffer;
  pack *server_message = receive_buffer;

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

  return server_message->size;
}


//Write nbyte bytes from buf into remote fd
size_t netwrite(int fd, const void * buf, size_t nbyte) {
  //No socket no conn
  if (socket_descriptor == -1) {
    h_errno = HOST_NOT_FOUND;
    return -1;
  }

  pack *client_message = send_buffer;
  pack *server_message = receive_buffer;

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

  return server_message->size;
}


int netclose(int fd) {
  //You get the drill by this point
  if (socket_descriptor == -1) {
    h_errno = HOST_NOT_FOUND;
    return -1;
  }

  pack *client_message = send_buffer;
  pack *server_message = receive_buffer;

  client_message->fp = fd;
  
  write(socket_descriptor, client_message, sizeof(pack));
  read(socket_descriptor, server_message, sizeof(pack));

  if (server_message->size == -1) {
    errno = server_message->op_code;
    return -1;
  }

  return 0;
}

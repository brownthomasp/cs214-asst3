#include <stdio.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <errno.h>
//#include <netdb.h> //Supposedly defines "HOST_NOT_FOUND"

int socket_descriptor = -1;
struct sockkaddr_in remote_server = NULL;
struct hostent *remote_ip = NULL;

/** On the off chance we end up having any need for this:

int getconnection() {
  if(remote_ip == NULL) {
    fprintf(stderr, "ERROR: cannot connect to a non-existent host.\n");
	return -1;
  }

  socket_descriptor = socket(AF_INET, SOCK_STREAM, 0);
  if (socket_descriptor < 0) {
    fprintf(stderr, "ERROR: could not create local socket.\n");
	return -2;
  }

  remoteServer.sin_family = AF_INET;
  bcopy((char *)remote_ip->h_addr, (char *)remoteServer.sin_addr.s_addr, remote_ip->h_length);
  remoteServer.sin_port = htons(9999);

  if (connect (socket_descriptor, (struct sockaddr *)&remote_server, sizeof(remote_server)) < 0) {
    fprintf(stderr, "ERROR: could not connect to remote server.\n");
	return -3;
  }

  return 0;
}

**/

int netserverinit(char * hostname) {
  remote_ip = gethostbyname(hostname);

  //Not 100% sure is that what the assignment description means by setting h_errno
  //(supposedly gethostbyname(<name>) will set h_errno if error occcurs)
  if (remote_ip == NULL) {
    h_errno = HOST_NOT_FOUND;
    return -1;
  }

  socket_descriptor = socket(AF_INET, SOCK_STREAM, 0);

  remoteServer.sin_family = AF_INET;
  bcopy((char *)remote_ip->h_addr, (char *)remoteServer.sin_addr.s_addr, remote_ip->h_length);
  remoteServer.sin_port = htons(9999);

  return 0;
}

int netopen(const char * pathname, int flags) {
  if (connect (socket_descriptor, (struct sockaddr *)&remote_server, sizeof(remote_server)) < 0) {
    fprintf(stderr, "ERROR: could not connect to remote server.\n");
	errno = EIO;
	return -1;
  }


}



size_t netread(int fd, const void * buf, size_t nbyte) {
  if (connect (socket_descriptor, (struct sockaddr *)&remote_server, sizeof(remote_server)) < 0) {
    fprintf(stderr, "ERROR: could not connect to remote server.\n");
	errno = EIO;
	return -1;
  }


}



size_t netwrite(int fd, const void * buf, size_t nbyte) {
  if (connect (socket_descriptor, (struct sockaddr *)&remote_server, sizeof(remote_server)) < 0) {
    fprintf(stderr, "ERROR: could not connect to remote server.\n");
	errno = EIO;
	return -1;
  }


}


int netclose(int fd) {
  if (connect (socket_descriptor, (struct sockaddr *)&remote_server, sizeof(remote_server)) < 0) {
    fprintf(stderr, "ERROR: could not connect to remote server.\n");
	errno = EIO;
	return -1;
  }


}

#include <stdio.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <pthread.h>
#include <string.h>
#include <stdlib.h>



typedef struct node {
  struct node * next;
  char * file_name;
  FILE * fp;
  pthread_mutex_t lock;
} node;

node * new_node(node * next, char * file_name) {
  node * new = malloc(sizeof(node));
  new->file_name = malloc(strlen(file_name) + 1);
  strcpy(new->file_name, file_name);
  pthread_mutex_init(&(new->lock));
  return new;
}

void clean_list(node * list) {
  node * ptr = list;
  node * next = list;
  while (next) {
    next = ptr->next;
    pthread_mutex_destroy(&(ptr->lock));
    free(ptr->file_name);
    free(ptr);
  }
}


static node * locks;



void * handle_connection(void * arg) {

  int * connection = arg;

  // receive request from client
  // search for and create if neded appropriate node/lock in lock list
  // get lock and open assosicated file descriptor
  // do requested stuff 
  // close file descriptor and unlock
  // close connection

  close(*connection);
  *connection = -1;

  return 0;

}


int main(int argc, char ** argv) {

  int socket_desc = socket(AF_INET, SOCK_STREAM, 0);
  if (socket_desc == -1) {
    fprintf(stderr, "ERROR: failed to aquire socket\n");
    return -1;
  }

  struct sockaddr_in server;
  server.sin_family = AF_INET;
  server.sin_addr.s_addr = INADDR_ANY;
  server.sin_port = htons(9999);

  if (bind(socket_desc, (struct sockaddr *)&server, sizeof(server)) < 0) {
    fprintf(stderr, "ERROR: failed to bind\n");
    return -1;
  }


  // arrays to store thread reffernece variables and connection socket descriptors
  // is there a better way to keep track of socket descriptors?
  pthread_t threads[100];
  int connections[100];
  int i;
  for (i = 0; i < 100; i++) { connections[i] = -1; }

  struct sockaddr_in client;
  int c = sizeof(struct sockaddr_in);

  while (1) {
    listen(socket_desc, 5);
    
    //find the first unused socket descriptor
    i = 0;
    while (connections[i] >= 0) { 
      i++;
      if (i == 100) {  }  // want to have this wait for an available descriptor

   }

    connections[i] = accept(socket_desc, (struct sockaddr *)&client, &c);
    if (connections[i] < 0) {
      fprintf(stderr, "ERROR: failed to accept\n");
    }

    if (!pthread_create(&threads[i], NULL, &handle_connection, (void *)&connections[i])) {
      fprintf(stderr, "ERROR: Failed to create thread for client connection\n");
    }

  }

  // how to we join threads if the loop does not exit?

  return 0;
}

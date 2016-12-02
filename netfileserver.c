#include <stdio.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <pthread.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>


// this struct is to format signals from and to the client
typedef struct pack {
  char file_name[128];
  int access_mode;
  int op_code;
  int fp;
  int size;
} pack;


// this struct is for a binary tree that will store the names of files that
// have been opened as well as a file desciptor, mutex lock, condition variable and 
// the IP address of the client who has the file open
typedef struct node {
  struct node * left;
  struct node * right;
  char * file_name;
  int fp;
  pthread_mutex_t lock;
  int IP;
  pthread_cond_t cond;
} node;


// this function initializes a new node and returns a pointer to it
static node * new_node(node * left, node * right, char * file_name) {
  node * new = malloc(sizeof(node));
  new->left = left;
  new->right = right;
  new->file_name = malloc(strlen(file_name) + 1);
  strcpy(new->file_name, file_name);
  pthread_mutex_init(&(new->lock), NULL);
  pthread_cond_init(&(new->cond), NULL);
  new->IP = 0;
  new->fp = 0;
  
  return new;
}


// this function will search the tree for the requested file and return the 
// assosicated node, creating the node if it doesn't already exist
static node * get_node(node * root, char * file_name) {
  if (!root) {
    root = new_node(NULL, NULL, file_name);
    return root;
  }

  if (!strcmp(file_name, root->file_name)) { return root; }
  else if (strcmp(file_name, root->file_name) < 0) { return get_node(root->left, file_name); }
  return get_node(root->right, file_name);

}

// this function also search for a node in the file tree with a given file descriptor
static node * search_fp(node * root, int fp) {
  if (!root || root->fp == fp) { return root; }
  node * node = search_fp(root->left, fp);
  if (node) { return node; }
  return search_fp(root->left, fp);

}


// this function cleans the tree
static void clean_tree(node * root) {
  if (!root) { return; }
  if (root->left) { clean_tree(root->left); }
  if (root->right) { clean_troo(root->right); }
  pthread_mutex_destroy(&(root->lock));
  pthread_cond_destroy(&(root->cond));
  free(root->file_name);
  free(root);
}

typedef struct connection {
  int sd; // socket descriptor
  long IP; // IP of client
} connection;

static connection connections[100];

static node * root;
static pthread_mutex_t root_lock = PTHREAD_MUTEX_INITIALIZER;


void * handle_connection(void * arg) {

  connection * con = arg;
  int files_open = 0;

  // receive request from client
  void * buffer = malloc(sizeof(pack));
  pack * input;
  node * node;

  read(con->sd, buffer, sizeof(pack));
  input = buffer;;

  switch (input->op_code) {
  case 0 : // open file
    // search the file tree for the given file and obtain the associated node
    pthread_mutex_lock(&root_lock);
    node = get_node(root, input->file_name);
    pthread_mutex_unlock(&root_lock);

    // wait for/claim the file condition variable and set the IP acces for that file
    while(node->IP != con->IP) {
      pthread_mutex_lock(&(node->lock));
      if (node->IP == 0) {
        node->IP = con->IP;
      } else {
        pthread_cond_wait(&(node->cond), &(node->lock));
      }
    }
    pthread_mutex_unlock(&(node->lock));
      
    node->fp = open(input->file_name, input->access_mode);
      
      // send back return values

    break;

  case 1: // read from file
    pthread_mutex_lock(&root_lock);
    node = search_fp(root, input->fp);
    pthread_mutex_unlock(&root_lock);
    if (!node) {
      // ERROR file descriptor not found
    } else if (node->IP != con->IP) {
      // ERROR client did not open this file descriptor
    } else {
      buffer = malloc(input->size);
      input->size = read(node->fp, buffer, input->size);
    }
    

    // send back return values

  case 2: // write to file
    pthread_mutex_lock(&root_lock);
    node = search_fp(root, input->fp);
    pthread_mutex_unlock(&root_lock);
    if (!node) {
      // ERROR file descriptor not found
    } else if (node->IP != con->IP) {
      // ERROR client did not open this file descriptor
    } else {
      buffer = malloc(input->size);
      read(con->sd, buffer, input->size);
      input->size = write(node->fp, buffer, input->size);
    }


    // send back return values

    break;

  case 3: // close file 
    pthread_mutex_lock(&root_lock);
    node = search_fp(root, input->fp);
    pthread_mutex_unlock(&root_lock);
    if (!node) {
      // ERROR file descriptor not found
    } else if (node->IP != con->IP) {
      // ERROR client did not open this file descriptor
    } else {
      pthread_mutex_lock(&(node->lock));
      input->size = close(node->fp);
      node->IP = 0;
      pthread_cond_signal(&(node->cond));
      pthread_mutex_unlock(&(node->lock));
    }


    // send back return values

    break;
    
  default :

    // ERROR

  }

  // free buffer and close connection
  free(buffer);
  close(con->sd);
  con->sd = -1;

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


  // array to store thread reffernece variables
  pthread_t threads[100];
  int i;
  for (i = 0; i < 100; i++) { connections[i].sd = -1; }

  struct sockaddr_in client;
  int c = sizeof(struct sockaddr_in);

  while (1) {
    listen(socket_desc, 5);
    
    //find the first unused socket descriptor
    i = 0;
    while (connections[i].sd >= 0) { 
      i++;
      if (i == 100) {  }  // want to have this wait for an available descriptor

   }

    connections[i].sd = accept(socket_desc, (struct sockaddr *)&client, &c);
    if (connections[i].sd < 0) {
      fprintf(stderr, "ERROR: failed to accept\n");
    }

    connections[i].IP = client.sin_addr.s_addr;

    if (!pthread_create(&threads[i], NULL, &handle_connection, (void *)&connections[i])) {
      fprintf(stderr, "ERROR: Failed to create thread for client connection\n");
    }

  }

  // how to we join threads if the loop does not exit?

  return 0;
}

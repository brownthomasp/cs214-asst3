#include <stdio.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <pthread.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <error.h>
#include <errno.h>

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
  new->fp = -1;
  
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
  if (root->right) { clean_tree(root->right); }
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

static int connection_count;
static pthread_mutex_t count_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t count_cond = PTHREAD_COND_INITIALIZER;


void * handle_connection(void * arg) {

  connection * con = (connection *)arg;
  //int files_open = 0;

  // receive request from client
  void * buffer = malloc(sizeof(pack));
  pack * input;
  node * node;

  read(con->sd, buffer, sizeof(pack));
  input = buffer;

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
    
    if (node->fp != -1) {
      //ERROR file already opened by client
      errno = EPERM;  //macro for "operation not permitted"
      input->size = -1;
    } else {
      node->fp = open(input->file_name, input->access_mode);
      input->size = node->fp;
    }

    input->op_code = errno;

    // send back return values
    write(con->sd, input, sizeof(pack));

    break;

  case 1: // read from file
    // obtain lock for access to the tree then search for the given file descriptor
    pthread_mutex_lock(&root_lock);
    node = search_fp(root, input->fp);
    pthread_mutex_unlock(&root_lock);
    if (!node || node->IP != con->IP) {
      // ERROR file descriptor was not opened by this client
      // either doesn't exist or was opened by another
      errno = EPERM; // macro for "operation not permitted"
    } else {
      buffer = malloc(input->size);
      pthread_mutex_lock(&(node->lock));
      input->size = read(node->fp, buffer, input->size);
      pthread_mutex_unlock(&(node->lock));
    }
    
    input->op_code = errno;

    // send back return values
    write(con->sd, input, sizeof(pack));
    if (input->size > 0) {
	write(con->sd, buffer, input->size);
    }

    break;

  case 2: // write to file
    // find the given file descriptor
    pthread_mutex_lock(&root_lock);
    node = search_fp(root, input->fp);
    pthread_mutex_unlock(&root_lock);
    if (!node || node->IP != con->IP) {
      // ERROR file descriptor was not opened by this client
      // either doesn't exist or opened by another
      errno = EPERM;
    } else {
      buffer = malloc(input->size);
      read(con->sd, buffer, input->size);
      pthread_mutex_lock(&(node->lock));
      input->size = write(node->fp, buffer, input->size);
      pthread_mutex_unlock(&(node->lock));
    }

    input->op_code = errno;

    // send back return values
    write(con->sd, input, sizeof(pack));

    break;

  case 3: // close file 
    pthread_mutex_lock(&root_lock);
    node = search_fp(root, input->fp);
    pthread_mutex_unlock(&root_lock);
    if (!node || node->IP != con->IP) {
      // ERROR client did not open this file descriptor
      errno = EPERM;
    } else {
      pthread_mutex_lock(&(node->lock));
      input->size = close(node->fp);
      node->fp = -1;
      node->IP = 0;
      pthread_cond_signal(&(node->cond));
      pthread_mutex_unlock(&(node->lock));
    }

    input->op_code = errno;

    // send back return values
    write(con->sd, input, sizeof(pack));

    break;
    
  //Default case: operation not permitted, return -1
  default :
    errno = EPERM;
    input->size = -1;
    input->op_code = errno;
    write(con->sd, input, sizeof(pack));
  }

  // free buffer and close connection
  free(buffer);
  close(con->sd);
  con->sd = -1;

  // decrament the connection count and signal
  pthread_mutex_lock(&count_lock);
  connection_count--;
  pthread_cond_signal(&count_cond);
  pthread_mutex_unlock(&count_lock);

  return 0;

}


int main(int argc, char ** argv) {

	//Set up socket for IPv4 (TCP/IP) connections.
  int socket_desc = socket(AF_INET, SOCK_STREAM, 0);
  if (socket_desc == -1) {
    fprintf(stderr, "ERROR: failed to acquire socket for incoming connections.\n");
    return -1;
  }

  //Get IPv4 address such that incoming connections will come to <address>:9999
  struct sockaddr_in server;
  server.sin_family = AF_INET;
  server.sin_addr.s_addr = INADDR_ANY;
  server.sin_port = htons(9999);

  //Bind address to previous socket.
  if (bind(socket_desc, (struct sockaddr *)&server, sizeof(server)) < 0) {
    fprintf(stderr, "ERROR: failed to bind address to socket.\n");
    return -2;
  }

  //Store child/worker threads (one per connection).
  pthread_t threads[100];
  int i;
  
  //Zero out connection socket descriptors.
  //Per described functionality, we want network descriptors to be all negative integers.
  //Positive integers may correspond to objects on the client's local filesystem.
  for (i = 0; i < 100; i++) { connections[i].sd = -1; }

  //Get address info of client as well size of the struct.
  struct sockaddr_in client;
  socklen_t client_size = sizeof(struct sockaddr_in);

  //Attempt to listen for incoming connections.
  //According to manpage of listen, this can fail if:
  //-another socket is listening on this port
  //-socket_desc is malformed
  if (listen(socket_desc, 5) < 0) {
    fprintf(stderr, "ERROR: cannot listen for connections on specified socket.\n");
	return -3;
  }
  
  //Changed this and nested while loop to use a descriptive name of what they do.
  //I am acknowledging that while(1) is functionally just as good.
  int accepting_new_connections = 1;
  while (accepting_new_connections) {
    //Spool over available connection "slots" and see if we can find an open one.
    //If there exists some sd such that sd = 0, that connection "slot" is available.
    i = 0;
    int searching_for_available_connection = 1;
    while (searching_for_available_connection) { 
      //When a thread finishes, it sets its connection->sd to -1, so we know if a slot is avaialble.
      if (connectors[i].sd == -1) {
        break;
      }
      
      i++;
      
      // on failure to find a free connection slot check the connection count and 
      // wait for a slot to be freed if there are none
      if (i == 100) {
	pthread_mutex_lock(&count_lock);
	if (connections_count == 100) {
	  pthread_cond_wait(&count_cond, &count_wait);
	}
	pthread_mutex_unlock(&count_lock);
	i = 0;
      }
    }

    //Try to accept the incoming connection in this "slot".
    //On failure, skip it and go back to looking for available slots.
    connections[i].sd = accept(socket_desc, (struct sockaddr *)&client, &client_size);
    if (connections[i].sd < 0) {
      fprintf(stderr, "ERROR: failed to accept incoming connection.\n");
        continue;
    }

    //Record the address of the connection
    connections[i].IP = client.sin_addr.s_addr;

    //Create thread. On error, go back to looking for connection slots.
    if (pthread_create(&threads[i], NULL, &handle_connection, (void *)&connections[i])) {
      fprintf(stderr, "ERROR: failed to create thread for client connection.\n");
      connections[i].sd = -1;
	  continue;
    }
	
    //Detach thread.
    if (pthread_detach(threads[i])) {
      fprintf(stderr, "ERROR: could not detach a worker thread.\n");
    }

    if (connections[i].sd > -1) {
      //Incrament the connection counter if there are no failures
      pthread_mutex_lock(&count_lock);
      connection_count++;
      pthread_mutex_unlock(&count_lock);
    }
	
  }

  clean_tree(root);

  return 0;
}

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
  int fd;
  int size;
} pack;


// this struct is a node for two binar trees, one will contain the clients with their connection permission
// and a list of the file descriptors they have open, the other will contain the files that have been opened 
// with a list of the file descriptors open for that file
typedef struct fnode {
  struct node * left;
  struct node * right;
  char * file_name;
  link * list;
  long IP;
  int perm;
  pthread_mutex_t lock;
} node;


// this function initializes a new node and returns a pointer to it
static node * new_node(node * left, node * right, char * file_name, long IP, int mode) {
  node * new = malloc(sizeof(node));
  new->left = left;
  new->right = right;
  if (file_name) { 
    new->file_name = malloc(strlen(file_name)+1);
    strcpy(new->file_name, file_name); 
  } else { new->file_name = NULL; }
  pthread_mutex_init(&(new->lock), NULL);
  new->IP = IP;
  new->perm = mode;
  new->list = NULL;
  
  return new;
}


// this function will search the file tree for the requested file and return the 
// assosicated node, creating the node if it doesn't already exist
static node * get_file(node * root, char * file_name) {
  if (!root) {
    root = new_node(NULL, NULL, file_name, 0, 0);
    return root;
  }

  if (!strcmp(file_name, root->file_name)) { return root; }
  else if (strcmp(file_name, root->file_name) < 0) { return get_file(root->left, file_name); }
  return get_file(root->right, file_name);

}

// this function will search the client tree for a given address and return the 
// associated node, this will be NULL if the node does not exist
static node * get_client(node * root, long IP) {
  if (!root) {
    return root;
  }

  if (root->IP == IP) { return root; }
  else if (root->IP < IP) { return get_client(root->left, IP); }
  return get_client(root->right, IP);

}

// this function is for adding a client to the client tree, if the client already 
// exists, it will update their permission mode
static void * add_client(node * root, long IP, int mode) {
  if (!root) {
    root = new_node(NULL, NULL, NULL, IP, mode);
    return;
  }

  if (root->IP == IP) { root->perm = mode; return; }
  else if (root->IP < IP) { return get_client(root->left, IP); }
  return get_client(root->right, IP);

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


// this struct is for the link lists that will be in both trees
typedef sturct link {
  struct link * next;
  int con_mode;  // connection mode of the client who owns this discriptor
  int access_mode;  // access_mode of the file descriptor
  int fd;    // the file descriptor
  node * file_node;  // pointer to the assosicated node in the file tree
} link;


// this function allocates and returns a pointer to a new link
static link * new_link(int fd, int con_mode, int access_mode, void * file_node) {
  link * new = malloc(sizeof(link));
  
  new->next = NULL;
  new->fd = fd;
  new->con_mode = con_mode;
  new->access_mode = access_mode;
  new->file_node = file_node;

  return new;
}

// this functin finds and removes the link for a given file descriptor from a given list
static void remove_link(list * root, int fd) {
  list * ptr = root;
  pist * prv = NULL;;
  
  while (ptr) {
    if (ptr->fd == fd) {
      if (prv) {
	prv->next = ptr->next;
	free(ptr);
      } else { 
	free(ptr);
	root = NULL; 
      }
    }
    ptr = ptr->next;
    prv = ptr;
  }
}



typedef struct connection {
  int sd; // socket descriptor
  long IP; // IP of client
} connection;

static connection connections[100];

static node * file_tree;
static pthread_mutex_t f_lock = PTHREAD_MUTEX_INITIALIZER;

static node * client_tree;
static pthread_mutex_t c_lock = PTHREAD_MUTEX_INITIALIZER;

static int connection_count;
static pthread_mutex_t count_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t count_cond = PTHREAD_COND_INITIALIZER;


void * handle_connection(void * arg) {

  connection * con = (connection *)arg;
  //int files_open = 0;

  // receive request from client
  void * buffer = malloc(sizeof(pack));
  pack * input;
  node * f_node;
  node * c_node;
  link * ptr;
  int valid = 1;

  read(con->sd, buffer, sizeof(pack));
  input = buffer;

  pthread_mutex_lock(&c_lock);
  c_node = get_client(client_tree, con->IP);
  pthread_mutex_unlock(&c_lock);

  if (!client_node) {
    // client has not run serverinit, has not established access mode
    input->op_code = -1; // force the switch to default
  } else {
    // obtain the lock for the client node
    pthread_mutex_lock(&(c_node->lock));
  }

  switch (input->op_code) {
  case 0 : // open file
    // search the file tree for the given file and obtain the associated node
    pthread_mutex_lock(&f_lock);
    node = get_node(root, input->file_name);
    pthread_mutex_unlock(&f_lock);

    // obtain the lock for the file node
    pthread_mutex_lock(&(f_node->lock));
   
    // check if the client has permission based on their connection mode which is the switch variable,
    // the access mode in which they want to open the file descriptor is then checked against the file
    // descriptors already open for that file
    ptr = f_node->list;
    switch (c_node->perm) {
    case 0:
      while (ptr) {
	if (ptr->con_mode == 2) { valid = 0; break; }
	if (ptr->con_mode == 1 && 
	    (ptr->access_mode == O_WRONLY || ptr->access_mode == O_RDWR) &&
	    (input->access_mode == O_WRONLY || input->access_mode == O_RDWR)) { valid = 0; break; }
	ptr = ptr->next;
      }

      break;

    case 1:
      while (ptr) {
	if (ptr->con_mode == 2) { valid = 0; break; }
	if ((input->access_mode == O_WRONLY || input->access_mode == O_RDWR) &&
	    (ptr->access_mode == O_WRONLY || ptr->access_mode == O_RDWR)) { valid = 0; break; }
	ptr = ptr->next;
      }
      break;

    case 2:
      if (!f_node->list) { valid = 0; }
      break;

    default:
      valid = 0;

    }

    while (ptr) {
      ptr = ptr->next;
    }

    if (valid) {
      // if the client can access the file, open a new file descriptor
      input->size = open(input->file_name, input->access_mode);
      // if the open was successful, add the new file descriptor to the lists
      // in the client and file nodes
      if (input->size != -1) {
	ptr = new_link(input->size, c_node->perm, input->access_mode, NULL);
	ptr = c_node->list;
	while (ptr) {
	  ptr = ptr->next; 
	}
	ptr = new_link(input->size, 0, 0, f_node);
      }
    } else {
      input->size = -1;
      errno = EPERM;
    }

    pthread_mutex_unlock(&(f_node->lock));

    input->op_code = errno;

    // send back return values
    write(con->sd, input, sizeof(pack));
    break;

  case 1: // read from file
    
    // find the desired file descriptor
    ptr = c_node->list;
    while (ptr) {
      if (ptr->fd == input->fd) { break; }
      ptr = ptr->next;
    }

    if (!ptr) {
      // ERROR file descriptor has not been made
      errno = EPERM; // macro for "operation not permitted"
    } else {
      buffer = malloc(input->size);
      input->size = read(ptr->fd, buffer, input->size);
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
    ptr = c_node->list;
    while (ptr) {
      if (ptr->fd == fd) { break; }
      ptr = ptr->next;
    }

    if (!ptr) {
      // ERROR file descriptor was not opened by this client
      // either doesn't exist or opened by another
      errno = EPERM;
    } else {
      buffer = malloc(input->size);
      read(con->sd, buffer, input->size);
      input->size = write(ptr->fd, buffer, input->size);
    }

    input->op_code = errno;

    // send back return values
    write(con->sd, input, sizeof(pack));

    break;

  case 3: // close file descriptor
 
    // find the given file descriptor
    ptr = c_node->list;
    while (ptr) {
      if (ptr->fd == fd) { break; }
      ptr = ptr->next;
    }
    if (!ptr) {
      // ERROR client did not open this file descriptor
      errno = EPERM;
    } else {
      // close the file descriptor
      input->size = close(ptr->fd);
      // if successful, clean up
      if (input->size != -1) {
	pthread_mutex_lock(&(ptr->file_node->lock));
	remove_link(ptr->file_node->list, input->fd);
	pthread_mutex_lock(&(ptr->file_node->lock));
	
	remove_link(c_node->list, input->fd);
      }
    }

    input->op_code = errno;

    // send back return values
    write(con->sd, input, sizeof(pack));

    break;

  case 4:  // this is for when a client initiallizes their connection to the server
    add_client(client_tree, con->IP, input->access_mode);

    break;
    
  //Default case: operation not permitted, return -1
  default :
    errno = EPERM;
    input->size = -1;
    input->op_code = errno;
    write(con->sd, input, sizeof(pack));
  }

  pthread_mutex_unlock(&(c_node->lock));

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

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

//Eliminates compile-time error regarding an fnode* being a member of linknode
//and a linknode* being a member of fnode. (Would say that linknode is undefined)
//Ideally all this should be in an .h file but its not in the project spec so yolo
struct fnode;
struct linknode;

//This struct is to format signals from and to the client
typedef struct pack {
  char file_name[128];
  int access_mode;
  int op_code;
  int fd;
  int size;
} pack;

// This struct is a node for two binary trees, one will contain the clients with their connection permission
// and a list of the file descriptors they have open, the other will contain the files that have been opened 
// with a list of the file descriptors open for that file
typedef struct fnode {
  struct fnode * left;
  struct fnode * right;
  char * file_name;
  struct linknode * list;
  long IP;
  int perm;
  pthread_mutex_t lock;
} node;

//This struct is for the linknode lists that will be in both trees
typedef struct linknode {
  struct linknode * next;
  int con_mode;  // connection mode of the client who owns this descriptor
  int access_mode;  // access_mode of the file descriptor
  int fd;    // the file descriptor
  node * file_node;  // pointer to the assosicated node in the file tree
} linknode;

//Relates a socket descriptor to IP
typedef struct connection {
  int sd; // socket descriptor
  long IP; // IP of client
} connection;

//This function initializes a new node and returns a pointer to it
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

// This function will search the file tree for the requested file and return the 
// associated node, creating the node if it doesn't already exist
static node * get_file(node * root, char * file_name) {
  if (!root) {
    root = new_node(NULL, NULL, file_name, 0, 0);
    return root;
  }

  if (!strcmp(file_name, root->file_name)) { return root; }
  else if (strcmp(file_name, root->file_name) < 0) { return get_file(root->left, file_name); }
  return get_file(root->right, file_name);

}

// This function will search the client tree for a given address and return the 
// associated node, this will be NULL if the node does not exist
static node * get_client(node * root, long IP) {
  if (!root) {
    return NULL;
  }

  if (root->IP == IP) { return root; }
  else if (root->IP < IP) { return get_client(root->left, IP); }
  return get_client(root->right, IP);

}

// This function is for adding a client to the client tree, if the client already 
// exists, it will update their permission mode
static node * add_client(node * root, long IP, int mode) {
  if (!root) {
    return new_node(NULL, NULL, NULL, IP, mode);
  }

  if (root->IP == IP) { root->perm = mode; }
  else if (root->IP < IP) { root->left = add_client(root->left, IP, mode); }
  root->right = add_client(root->right, IP, mode);

  return root;
}

// This function cleans the tree
static void clean_tree(node * root) {
  if (!root) { return; }
  if (root->left) { clean_tree(root->left); }
  if (root->right) { clean_tree(root->right); }
  pthread_mutex_destroy(&(root->lock));
 // pthread_cond_destroy(&(root->cond)); which condition were we talking about?
  free(root->file_name);
  free(root);
}


// This function allocates and returns a pointer to a new linknode
static linknode * new_linknode(int fd, int con_mode, int access_mode, void * file_node) {
  linknode * new = malloc(sizeof(linknode));
  
  new->next = NULL;
  new->fd = fd;
  new->con_mode = con_mode;
  new->access_mode = access_mode;
  new->file_node = file_node;

  return new;
}

static void print_single_linknode(linknode *head) {
  if (!head) return;

  printf("\tlinknode node @ %p\n", head);
  printf("\t\tcon mode = %d\n", head->con_mode);
  printf("\t\taccess mode = %d\n", head->access_mode);
  printf("\t\tfd = %d\n", head->fd);
  printf("\t\tassoc file node @ %p\n", head->file_node);
}

// This functin finds and removes the linknode for a given file descriptor from a given list
static void remove_linknode(linknode * root, int fd) {
  linknode * ptr = root;
  linknode * prv = NULL;
 
  printf("trying to remove node with fd = %d\n", fd);
 
  while (ptr) {
    printf("current node is:\n");
    print_single_linknode(ptr);
    
    if (ptr->fd == fd) {
      if (prv) {
	prv->next = ptr->next;
	free(ptr);
        break;
      } else { 
	free(ptr);
	root = NULL;
        break; 
      }
    }

    prv = ptr; 
    ptr = ptr->next;
   
  }
}

static linknode * add_to_end(linknode *head, linknode *new) {
  if(!head) return new;
  
  linknode *ptr = head, *prev = NULL;  

  while(ptr) {
    prev = ptr;
    ptr = ptr->next;
  }

  prev->next = new;

  return head;
}

static void print_list(linknode *head) {
  if (!head) return;

  printf("\tlinknode node @ %p\n", head);
  printf("\t\tcon mode = %d\n", head->con_mode);
  printf("\t\taccess mode = %d\n", head->access_mode);
  printf("\t\tfd = %d\n", head->fd);
  printf("\t\tassoc file node @ %p\n", head->file_node);

  print_list(head->next);
}

static void print_btree(node *root) {
  if (!root) return;

  print_btree(root->left);
  printf("Btree node @ %p\n", root);
  printf("\tleft child @ %p\n", root->left);
  printf("\tright child @ %p\n", root->right);
  printf("\tfname = %s\n", root->file_name);
  print_list(root->list);
  printf("\tip = %ld\n", root->IP);
  printf("\tperm = %d (0 unrestricted, 1 exclusive, 2 transaction)\n", root->perm);
  print_btree(root->left);
}

static void print_btree_singlenode(node *root) {
  if(!root) {
    printf("Node is null --> this client has not yet connected\n");
    return;
  }

  printf("Btree node @ %p\n", root);
  printf("\tleft child @ %p\n", root->left);
  printf("\tright child @ %p\n", root->right);
  printf("\tfname = %s\n", root->file_name);
  print_list(root->list);
  printf("\tip = %ld\n", root->IP);
  printf("\tperm = %d (0 unrestricted, 1 exclusive, 2 transaction)\n", root->perm);
}
/////





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
  linknode * ptr;
  int valid = 1;

  read(con->sd, buffer, sizeof(pack));
  input = buffer;

  pthread_mutex_lock(&c_lock);
  c_node = get_client(client_tree, con->IP);
  pthread_mutex_unlock(&c_lock);

  printf("Printing client node:\n");
  print_btree_singlenode(c_node);

  if (!c_node) {
    // client has not run serverinit, has not established access mode
    printf("Client did not run netserverinit no bueno\n");
    if(input->op_code != 4) input->op_code = -1; // force the switch to default
  } else {
    // obtain the lock for the client node
    pthread_mutex_lock(&(c_node->lock));
  }

  switch (input->op_code) {
  case 0 : // open file
    // search the file tree for the given file and obtain the associated node
    printf("Got open command.\n");
    pthread_mutex_lock(&f_lock);
    f_node = get_file(file_tree, input->file_name);
    pthread_mutex_unlock(&f_lock);

    printf("Printing file node:\n");
    print_btree_singlenode(f_node);

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
      
      break;
    }

  //  while (ptr->next) {
  //   ptr = ptr->next;
  //  }

    if (valid) {
      // if the client can access the file, open a new file descriptor
      input->size = open(input->file_name, input->access_mode | O_APPEND);
      // if the open was successful, add the new file descriptor to the lists
      // in the client and file nodes
      if (input->size != -1) {
	f_node->list = add_to_end(f_node->list, new_linknode(input->size, c_node->perm, input->access_mode, NULL));
//	ptr = c_node->list;
//	while (ptr->next) {
//	  ptr = ptr->next; 
//	}
	c_node->list = add_to_end(c_node->list, new_linknode(input->size, 0, 0, f_node));
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
    printf("Got read command.\n");
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
      lseek(ptr->fd, 0, SEEK_SET); //for every read, reset to 0
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
      if (ptr->fd == input->fd) { break; }
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
    printf("Got close command.\n");
    // find the given file descriptor
    ptr = c_node->list;
    
    printf("Searching for given file.\n");
    while (ptr) {
      if (ptr->fd == input->fd) { break; }
      ptr = ptr->next;
    }
    if (!ptr) {
      // ERROR client did not open this file descriptor
      printf("This client did not open this fildes.\n");
      errno = EPERM;
    } else {
      printf("Trying to close fd...\n");
      // close the file descriptor
      input->size = close(ptr->fd);
      printf("Closed.\n");
      // if successful, clean up
      if (input->size != -1) {
	pthread_mutex_lock(&(ptr->file_node->lock));
	printf("Doing weird removal A\n");
        remove_linknode(ptr->file_node->list, input->fd);
	pthread_mutex_unlock(&(ptr->file_node->lock));
	
        printf("Doing weird removal B\n");
	remove_linknode(c_node->list, input->fd);
      }
    }

    input->op_code = errno;

    // send back return values
    write(con->sd, input, sizeof(pack));

    break;

  case 4:  // this is for when a client initiallizes their connection to the server
   printf("Got init command.\n");
   client_tree = add_client(client_tree, con->IP, input->access_mode);
   printf("Added client to client list.\n");

   break;
    
  //Default case: operation not permitted, return -1
  default:
    printf("Client did not send an opcode, or opcode was not recognized.\n");
    errno = EPERM;
    input->size = -1;
    input->op_code = errno;
    write(con->sd, input, sizeof(pack));
  }

  if (c_node != NULL) pthread_mutex_unlock(&(c_node->lock));

  // free buffer and close connection
  free(buffer);
  close(con->sd);
  con->sd = -1;

  printf("Closed local command buffer for this client.\n");

  // decrament the connection count and signal
  pthread_mutex_lock(&count_lock);
  connection_count--;
  pthread_cond_signal(&count_cond);
  pthread_mutex_unlock(&count_lock);

  printf("Signaled that this command has completed.\n");

  return 0;

}


int main(int argc, char ** argv) {

  //Set up socket for IPv4 (TCP/IP) connections.
  int socket_desc = socket(AF_INET, SOCK_STREAM, 0);
  if (socket_desc == -1) {
    fprintf(stderr, "ERROR: failed to acquire socket for incoming connections.\n");
    return -1;
  } else printf("Successfully acquired socket.\n");

  //Get IPv4 address such that incoming connections will come to <address>:9999
  struct sockaddr_in server;
  server.sin_family = AF_INET;
  server.sin_addr.s_addr = INADDR_ANY;
  server.sin_port = htons(9999);

  //Bind address to previous socket.
  if (bind(socket_desc, (struct sockaddr *)&server, sizeof(server)) < 0) {
    fprintf(stderr, "ERROR: failed to bind address to socket.\n");
    return -2;
  } else printf("Successfully bound address to socket.\n");

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
  } else printf("Listening for connections...\n\n");
  
  //Changed this and nested while loop to use a descriptive name of what they do.
  //I am acknowledging that while(1) is functionally just as good.
  int accepting_new_connections = 1;
  int searching_for_available_connection = 1;
  while (accepting_new_connections) {
    //Spool over available connection "slots" and see if we can find an open one.
    //If there exists some sd such that sd = 0, that connection "slot" is available.
    i = 0;
    while (searching_for_available_connection) { 
      //When a thread finishes, it sets its connection->sd to -1, so we know if a slot is avaialble.
      if (connections[i].sd == -1) {
       // printf("First available connection is in slot %d.\n", i);
        break;
      }
      
      i++;
      
      // on failure to find a free connection slot check the connection count and 
      // wait for a slot to be freed if there are none
      if (i >= 100) {
        printf("Could not find an open slot. Waiting on a slot to open up.\n");
	
        pthread_mutex_lock(&count_lock);
	
        if (connection_count == 100) {
	  pthread_cond_wait(&count_cond, &count_lock);
	}
        
        printf("A slot opened. Finding available connection...\n");
	
        pthread_mutex_unlock(&count_lock);
	i = 0;
        break;
      }
    }

    // printf("Trying to accept connection at slot %d:\n", i);
    //Try to accept the incoming connection in this "slot".
    //On failure, skip it and go back to looking for available slots.
    connections[i].sd = accept(socket_desc, (struct sockaddr *)&client, &client_size);
    if (connections[i].sd < 0) {
      fprintf(stderr, "ERROR: failed to accept incoming connection.\n");
        continue;
    } else printf("Successfully accepted new connection in slot %d.\n", i);

    //Record the address of the connection
    connections[i].IP = client.sin_addr.s_addr;

    //Create thread. On error, go back to looking for connection slots.
    if (pthread_create(&threads[i], NULL, &handle_connection, (void *)&connections[i])) {
      fprintf(stderr, "ERROR: failed to create thread for client connection.\n");
      connections[i].sd = -1;
	  continue;
    } //else printf("Successfully created thread to handle new connection.\n");
	
    //Detach thread.
    if (pthread_detach(threads[i])) {
      fprintf(stderr, "ERROR: could not detach a worker thread.\n");
    } //else printf("Successfully detached the thread.\n");

    if (connections[i].sd > -1) {
      //Increment the connection counter if there are no failures
      pthread_mutex_lock(&count_lock);
      connection_count++;
      pthread_mutex_unlock(&count_lock);
    }
	
  }

  return 0;
}

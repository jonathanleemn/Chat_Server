#include "blather.h"
#include <pthread.h>
#include <semaphore.h>

// Gets a pointer to the client_t struct at the given index. If the
// index is beyond n_clients, the behavior of the function is
// unspecified and may cause a program crash.
client_t *server_get_client(server_t *server, int idx) {
  return &server->client[idx];
}

// Initializes and starts the server with the given name. A join fifo
// called "server_name.fifo" should be created. Removes any existing
// file of that name prior to creation. Opens the FIFO and stores its
// file descriptor in join_fd._
//
// ADVANCED: create the log file "server_name.log" and write the
// initial empty who_t contents to its beginning. Ensure that the
// log_fd is position for appending to the end of the file. Create the
// POSIX semaphore "/server_name.sem" and initialize it to 1 to
// control access to the who_t portion of the log.
void server_start(server_t *server, char *server_name, int perms) {
  // removes existing FIFO
  remove(server->server_name);

  // creates and opens FIFO correctly for joins,
  sprintf(server->server_name, "%s.fifo", server_name);
  int retval = mkfifo(server->server_name, perms);
  check_fail(retval == -1, 1, "mkfifo() failed\n");
  int requests_fd = open(server->server_name, O_RDWR);
  check_fail(requests_fd == -1, 1, "Failed to open requests file\n");


  server->join_fd = requests_fd;
  server->join_ready = 0;
  server->n_clients = 0;
  server->time_sec = 0;
}

// Shut down the server. Close the join FIFO and unlink (remove) it so
// that no further clients can join. Send a BL_SHUTDOWN message to all
// clients and proceed to remove all clients in any order.
//
// ADVANCED: Close the log file. Close the log semaphore and unlink
// it.
void server_shutdown(server_t *server) {
  // closes and removes FIFO to prevent additional joins
  int c_val = close(server->join_fd);
  int u_val = unlink(server->server_name);

  // basic error checking of system calls
  check_fail(c_val == -1, 1, "Failed close fifo\n");
  check_fail(u_val == -1, 1, "Failed unlink fifo\n");

  // broadcasts a shutdown message
  mesg_t mesg;
  memset(&mesg, 0, sizeof(mesg_t));
  mesg.kind = BL_SHUTDOWN;
  server_broadcast(server, &mesg);

  for(int i = 0; i < server->n_clients; i++) {
    server_remove_client(server, i);
  }

}

// Adds a client to the server according to the parameter join which
// should have fileds such as name filed in.  The client data is
// copied into the client[] array and file descriptors are opened for
// its to-server and to-client FIFOs. Initializes the data_ready field
// for the client to 0. Returns 0 on success and non-zero if the
// server has no space for clients (n_clients == MAXCLIENTS).
int server_add_client(server_t *server, join_t *join) {
  // bounds checking to prevent overflow on add
  if (server->n_clients == MAXCLIENTS)
    return 1;
  else {
    // uses server_get_client() for readability
    client_t *client = server_get_client(server, server->n_clients);

    // fills in fixed fields of clienta data based on join parameter
    // makes use of strncpy() to prevent buffer overruns
    memset(client->name, '\0', sizeof(client->name));
    strncpy(client->name, join->name, strlen(join->name));

    memset(client->to_client_fname, '\0', sizeof(client->to_client_fname));
    strncpy(client->to_client_fname, join->to_client_fname, strlen(join->to_client_fname));

    memset(client->to_server_fname, '\0', sizeof(client->to_server_fname));
    strncpy(client->to_server_fname, join->to_server_fname, strlen(join->to_server_fname));

    // adds client to end of array and increments n_clients
    server->client[server->n_clients] = *client;
    server->n_clients++;

    // opens to-client and to-server FIFOs for reading
    client->to_client_fd = open(client->to_client_fname, O_RDWR);
    check_fail(client->to_client_fd == -1, 1, "Failed to open to_client file\n");
    client->to_server_fd = open(client->to_server_fname, O_RDWR);
    check_fail(client->to_server_fd == -1, 1, "Failed to open to_server file\n");

    client->data_ready = 0;

    return 0;
  }
}

// Remove the given client likely due to its having departed or
// disconnected. Close fifos associated with the client and remove
// them.  Shift the remaining clients to lower indices of the client[]
// array and decrease n_clients.
int server_remove_client(server_t *server, int idx) {
  // uses server_get_client() for readability
  client_t *client = server_get_client(server, idx);

  printf("server_remove_client(): %d\n", idx);
  // closes to-client and from-client FIFOs
  remove(client->to_client_fname);
  remove(client->to_server_fname);
  close(client->to_client_fd);
  close(client->to_server_fd);

  // shifts array of clients to maintain contiguous client array and order of joining
  server->n_clients = server->n_clients - 1;
  for(int i = idx; i < server->n_clients; i++)
     server->client[i] = server->client[i+1];

  return 0;
}

// Send the given message to all clients connected to the server by
// writing it to the file descriptors associated with them.
//
// ADVANCED: Log the broadcast message unless it is a PING which
// should not be written to the log.
int server_broadcast(server_t *server, mesg_t *mesg) {
  for(int i = 0; i < server->n_clients; i++) {
    // uses server_get_client() for readability
    client_t *client = server_get_client(server, i);
    // memset(client->to_client_fd, 0, sizeof(client_t));
    write(client->to_client_fd, mesg, sizeof(mesg_t));
  }
  return 0;
}

// Checks all sources of data for the server to determine if any are
// ready for reading. Sets the servers join_ready flag and the
// data_ready flags of each of client if data is ready for them.
// Makes use of the select() system call to efficiently determine
// which sources are ready.
void server_check_sources(server_t *server) {
  // sets read flags for join and clients
  fd_set read_set;
  FD_ZERO(&read_set);
  FD_SET(server->join_fd, &read_set);

  // checks join FIFO and all clients in select() call
  int maxfd = server->join_fd;
  for(int i = 0; i < server->n_clients; i++) {
    client_t *client = server_get_client(server, i);

    FD_SET(client->to_server_fd, &read_set);
    // if maxfd < client->to_server_fd
    //  then maxfd = client->to_server_fd
    // else maxfd = maxfd
    maxfd = (maxfd < client->to_server_fd ? client->to_server_fd : maxfd);
  }

  // makes use of select() system call to detect ready clients/joins
  int retval = select(maxfd+1, &read_set, NULL, NULL, NULL);

  // basic error checking
  check_fail(retval == -1, 1, "select() failed\n");

  //sets read flags for join and clients
  for(int i = 0; i < server->n_clients; i++) {
    client_t *client = server_get_client(server, i);
    if(FD_ISSET(client->to_server_fd, &read_set))
      client->data_ready = 1;
  }
  if(FD_ISSET(server->join_fd, &read_set))
    server->join_ready = 1;
}

// Return the join_ready flag from the server which indicates whether
// a call to server_handle_join() is safe.
int server_join_ready(server_t *server) {
  return server->join_ready;
}

// Call this function only if server_join_ready() returns true. Read a
// join request and add the new client to the server. After finishing,
// set the servers join_ready flag to 0.
int server_handle_join(server_t *server) {
  if (server_join_ready(server) == 1) {
    // reads a join_t from join FIFO
    printf("server_process_join()\n");
    join_t join;
    int nread = read(server->join_fd, &join, sizeof(join_t));
    check_fail(nread == -1, 1, "read() failed\n");

    // adds client with server_add_client()
    server_add_client(server, &join);
    client_t *client = server_get_client(server, server->n_clients-1);
    printf("server_process_add_client(): %s %s %s \n", client->name,
      client->to_client_fname, client->to_server_fname);

    // broadcasts join
    mesg_t mesg;
    memset(&mesg, 0, sizeof(mesg_t));
    strcpy(mesg.name, client->name);
    mesg.kind = BL_JOINED;
    server_broadcast(server, &mesg);
    printf("server_broadcast(): %d from %s -\n", mesg.kind, client->name);

    server->join_ready = 0;
    return 0;
  }
  else
    return 1;
}

// Return the data_ready field of the given client which indicates
// whether the client has data ready to be read from it.
int server_client_ready(server_t *server, int idx) {
  client_t *client = server_get_client(server, idx);
  return client->data_ready;
}

// Process a message from the specified client. This function should
// only be called if server_client_ready() returns true. Read a
// message from to_server_fd and analyze the message kind. Departure
// and Message types should be broadcast to all other clients.  Ping
// responses should only change the last_contact_time below. Behavior
// for other message types is not specified. Clear the client's
// data_ready flag so it has value 0.
//
// ADVANCED: Update the last_contact_time of the client to the current
// server time_sec.
int server_handle_client(server_t *server, int idx) {
  if (server_client_ready(server, idx) == 1) {
    // uses server_get_client() for readability
    client_t *client = server_get_client(server, idx);

    // reads a mesg_t from the to-server FIFO
    mesg_t mesg;
    memset(&mesg, 0, sizeof(mesg_t));
    int nread = read(client->to_server_fd, &mesg, sizeof(mesg_t));
    // does basic error checking of system calls
    check_fail(nread == -1, 1, "read() failed\n");

    // processes message properly and broadcasts if needed
    if(mesg.kind == BL_MESG) {
      server_broadcast(server, &mesg);
      printf("server: mesg received from client %d %s : %s\n",
        idx, client->name, mesg.body);
      printf("server_broadcast(): %d from %s -\n", mesg.kind, client->name);
    }

    if(mesg.kind == BL_DEPARTED) {
      strcpy(mesg.name, client->name);
      server_remove_client(server, idx);
      server_broadcast(server, &mesg);
    }

    client->data_ready = 0;
    client->last_contact_time = server->time_sec;
    return 0;
  }
  else
    return 1;
}

// ADVANCED: Increment the time for the server
void server_tick(server_t *server) {
  server->time_sec++;
}

// ADVANCED: Ping all clients in the server by broadcasting a ping.
void server_ping_clients(server_t *server) {
  mesg_t mesg;
  memset(&mesg, 0, sizeof(mesg_t));
  mesg.kind = BL_PING;
  server_broadcast(server, &mesg);
}

// ADVANCED: Check all clients to see if they have contacted the
// server recently. Any client with a last_contact_time field equal to
// or greater than the parameter disconnect_secs should be
// removed. Broadcast that the client was disconnected to remaining
// clients.  Process clients from lowest to highest and take care of
// loop indexing as clients may be removed during the loop
// necessitating index adjustments.
void server_remove_disconnected(server_t *server, int disconnect_secs) {
  for(int i = 0; i < server->n_clients; i++) {
    if(server->client[i].last_contact_time >= disconnect_secs)
      server_remove_client(server, i);
  }
  mesg_t mesg;
  memset(&mesg, 0, sizeof(mesg_t));
  mesg.kind = BL_DISCONNECTED;
  server_broadcast(server, &mesg);
}

// ADVANCED: Write the current set of clients logged into the server
// to the BEGINNING the log_fd. Ensure that the write is protected by
// locking the semaphore associated with the log file. Since it may
// take some time to complete this operation (acquire semaphore then
// write) it should likely be done in its own thread to prevent the
// main server operations from stalling.  For threaded I/O, consider
// using the pwrite() function to write to a specific location in an
// open file descriptor which will not alter the position of log_fd so
// that appends continue to write to the end of the file.
void server_write_who(server_t *server) {
  char *sem_name = "lock";
  sem_t *sem = sem_open(sem_name, O_CREAT, DEFAULT_PERMS);
  for(int i = 0; i < server->n_clients; i++) {
    sem_wait(sem);
    // write(server->log_fd, server->client[i].to_client_fd,
    //   sizeof(server->client[i].to_client_fd));
    sem_post(sem);
  }
}

// ADVANCED: Write the given message to the end of log file associated
// with the server.
void server_log_message(server_t *server, mesg_t *mesg) {
  write(server->log_fd, mesg, sizeof(mesg_t));
}

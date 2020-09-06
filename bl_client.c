#include "blather.h"
#include <time.h>

simpio_t simpio_actual;
simpio_t *simpio = &simpio_actual;

pthread_t user_thread;
pthread_t server_thread;

join_t join;

int server_fd;
int to_server_fd;
int to_client_fd;

// user thread{
//   repeat:
//     read input using simpio
//     when a line is ready
//     create a mesg_t with the line and write it to the to-server FIFO
//   until end of input
//   write a DEPARTED mesg_t into to-server
//   cancel the server thread
void *user_worker(void *arg) {
  mesg_t mesg;
  memset(&mesg, 0, sizeof(mesg_t));
  while(1) {
    simpio_reset(simpio);
    iprintf(simpio, "");

    while(!simpio->line_ready) {
      simpio_get_char(simpio);
      if(simpio->end_of_input)
        break;
    }
    if(simpio->end_of_input)
      break;

    if(simpio->line_ready) {
      strcpy(mesg.name, join.name);
      strcpy(mesg.body, simpio->buf);
      mesg.kind = BL_MESG;
      // user thread sends data to server
      write(to_server_fd, &mesg, sizeof(mesg_t));
    }
  }
  mesg.kind = BL_DEPARTED;
  // user thread sends data to server
  write(to_server_fd, &mesg, sizeof(mesg_t));
  // cancels server thread at end of input before returning
  pthread_cancel(server_thread);
  return NULL;
}

// server thread{
//   repeat:
//     read a mesg_t from to-client FIFO
//     print appropriate response to terminal with simpio
//   until a SHUTDOWN mesg_t is read
//   cancel the user thread
void *server_worker(void *arg) {
  mesg_t mesg;
  memset(&mesg, 0, sizeof(mesg_t));
  while(1) {
    read(to_client_fd, &mesg, sizeof(mesg_t));

    // handles different kinds of messages coming from server
    if (mesg.kind == BL_SHUTDOWN) {
      iprintf(simpio, "!!! server is shutting down !!!\n");
      break;
    }
    else if (mesg.kind == BL_JOINED)
      iprintf(simpio, "-- %s JOINED --\n", mesg.name);
    else if (mesg.kind == BL_MESG)
      iprintf(simpio, "[%s] : %s\n", mesg.name, mesg.body);
    else if (mesg.kind == BL_DEPARTED)
      iprintf(simpio, "-- %s DEPARTED --\n", mesg.name);
  }
  // cancels user thread on receiving shutdown message and returns
  pthread_cancel(user_thread);
  return NULL;
}

int main(int argc, char** argv) {
  if(argc < 2) {
    printf("Too few arguments in command line\n");
    exit(1);
  }

  char prompt[MAXNAME];
  snprintf(prompt, MAXNAME, "%s>> ", argv[2]);
  simpio_set_prompt(simpio, prompt);         // set the prompt
  simpio_reset(simpio);                      // initialize io
  simpio_noncanonical_terminal_mode();

  // read name of server and name of user from command line args
  pid_t pid = getpid();
  sprintf(join.name, "%s", argv[2]);
  sprintf(join.to_client_fname, "%d.client.fifo", pid);
  sprintf(join.to_server_fname, "%d.server.fifo", pid);

  // create to-server and to-client FIFOs
  mkfifo(join.to_server_fname, DEFAULT_PERMS);
  mkfifo(join.to_client_fname, DEFAULT_PERMS);

  // write a join_t request to the server FIFO
  char server_fifo[MAXPATH] = "";
  sprintf(server_fifo, "%s.fifo", argv[1]);
  server_fd = open(server_fifo, O_RDWR);
  write(server_fd, &join, sizeof(join_t));

  to_client_fd = open(join.to_client_fname, O_RDWR);
  to_server_fd = open(join.to_server_fname, O_RDWR);

  // start a user thread to read input
  pthread_create(&user_thread, NULL, user_worker, NULL);
  // start a server thread to listen to the server
  pthread_create(&server_thread, NULL, server_worker, NULL);

  // wait for threads to return
  pthread_join(user_thread, NULL);
  pthread_join(server_thread, NULL);

  // restore standard terminal output
  simpio_reset_terminal_mode();
  printf("\n");
  return 0;
}

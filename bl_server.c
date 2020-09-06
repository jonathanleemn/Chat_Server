#include "blather.h"

// REPEAT:
//   check all sources
//   handle a join request if one is ready (add client function)
//   for each client{
//     if the client is ready handle data from it
//   }
// }
server_t server_actual;
server_t *server = &server_actual;

int shutdown = 0;

void handle_SIGINT(int sig_num) {
  shutdown = 1;
  server_shutdown(server);
}

void handle_SIGTERM(int sig_num) {
  shutdown = 1;
  server_shutdown(server);
}


int main(int argc, char** argv) {
  if(argc < 1) {
    printf("Too few arguments in command line\n");
    exit(1);
  }
  setvbuf(stdout, NULL, _IONBF, 0);
  // signal handling for graceful shutdown
  signal(SIGINT, handle_SIGINT);
  signal(SIGTERM, handle_SIGTERM);

  printf("server_start()\n");
  server_start(server, argv[1], DEFAULT_PERMS);
  printf("server_start(): end\n");

  while(!shutdown) {
    // check input sources
    server_check_sources(server);
    if(server_join_ready(server) == 1) {
      server_handle_join(server);
    }
    // handle ready input
    for(int i = 0; i < server->n_clients; i++) {
      if(server_client_ready(server, i) == 1) {
        server_handle_client(server, i);
      }
    }
  }
  return 0;
}

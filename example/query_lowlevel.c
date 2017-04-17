#include <stdio.h>
#include <stdlib.h>
#include <netdb.h>
#include <sys/types.h>
#include <sys/queue.h>

#include <libpq-fe.h>

#include <dynamic.h>
#include <reactor.h>

#include "reactor_postgres.h"

void event(void *state, int type, void *data)
{
  reactor_postgres *p = state;
  PGresult *result;

  switch (type)
    {
    case REACTOR_POSTGRES_EVENT_READY:
      (void) fprintf(stderr, "[ready]\n");
      reactor_postgres_send(p, "SELECT * FROM world WHERE id = 1");
      break;
    case REACTOR_POSTGRES_EVENT_CLOSE:
      (void) fprintf(stderr, "[close]\n");
      break;
    case REACTOR_POSTGRES_EVENT_QUERY_BAD:
      (void) fprintf(stderr, "[bad query] %s\n", (char *) data);
      reactor_postgres_close(p);
      break;
    case REACTOR_POSTGRES_EVENT_RESULT:
      (void) fprintf(stderr, "[result]\n");
      result = data;
      printf("%u\n", ntohl(*((uint32_t *) PQgetvalue(result, 0, 1))));
      break;
    case REACTOR_POSTGRES_EVENT_QUERY_DONE:
      reactor_postgres_close(p);
      break;
    case REACTOR_POSTGRES_EVENT_ERROR:
      (void) fprintf(stderr, "[error] '%s'\n", (char *) data);
      reactor_postgres_close(p);
      break;
    }
}

int main()
{
  reactor_postgres p;

  reactor_core_construct();
  reactor_postgres_open(&p, event, &p,
                        (const char *[]){"host", "user", NULL},
                        (const char *[]){"127.0.0.1", "postgres", NULL});
  reactor_core_run();
  reactor_core_destruct();
}

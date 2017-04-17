#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <netdb.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/queue.h>
#include <err.h>

#include <libpq-fe.h>

#include <dynamic.h>
#include <clo.h>
#include <reactor.h>

#include "reactor_postgres.h"

typedef struct app app;
struct app
{
  reactor_postgres_client        client;
  reactor_timer                  timer;
  size_t                         queries;
};

typedef struct app_query app_query;
struct app_query
{
  app                           *app;
  reactor_postgres_client_query  query;
};

static void app_stats(app *app)
{
  reactor_postgres_client_stats stats;

  reactor_postgres_client_get_stats(&app->client, &stats);
  (void) fprintf(stderr, "[stats] connections %lu/%lu, qeuries %lu/%lu\n",
                 stats.connections_busy, stats.connections,
                 stats.queries_queued, stats.queries_completed);
}

static void query_event(void *state, int type, void *data)
{
  app_query *q = state;
  PGresult *result;

  switch (type)
    {
    case REACTOR_POSTGRES_CLIENT_QUERY_EVENT_RESULT:
      result = data;
      (void) result;
      break;
    case REACTOR_POSTGRES_CLIENT_QUERY_EVENT_BAD:
      (void) fprintf(stderr, "[query] bad\n");
      break;
    case REACTOR_POSTGRES_CLIENT_QUERY_EVENT_ABORT:
      (void) fprintf(stderr, "[query] abort\n");
      break;
    case REACTOR_POSTGRES_CLIENT_QUERY_EVENT_CLOSE:
      q->app->queries --;
      if (!q->app->queries)
        {
          app_stats(q->app);
          reactor_postgres_client_close(&q->app->client);
          reactor_timer_close(&q->app->timer);
        }
      free(q);
      break;
    }
}


static void timer_event(void *state, int type, void *data)
{
  app *app = state;

  (void) data;
  switch (type)
    {
    case REACTOR_TIMER_EVENT_CALL:
      app_stats(app);
      break;
    }
}

static void client_event(void *state, int type, void *data)
{
  (void) state;
  (void) data;
  switch (type)
    {
    case REACTOR_POSTGRES_CLIENT_EVENT_ERROR:
      (void) fprintf(stderr, "[client] error\n");
      break;
    case REACTOR_POSTGRES_CLIENT_EVENT_CLOSE:
      (void) fprintf(stderr, "[client] close\n");
      break;
    }
}

void usage()
{
  extern char *__progname;

  (void) fprintf(stderr, "usage: %s <parallel> <count> <command>\n", __progname);
  exit(1);
}

int main(int argc, char **argv)
{
  app app = {0};
  app_query *q;
  size_t i, p, n;
  char *command;

  if (argc != 4)
    usage();
  p = strtoul(argv[1], NULL, 0);
  n = strtoul(argv[2], NULL, 0);
  command = argv[3];

  reactor_core_construct();
  reactor_timer_open(&app.timer, timer_event, &app, 100000000, 100000000);
  reactor_postgres_client_open(&app.client, client_event, &app,
                               (const char *[]){"host", "user", NULL},
                               (const char *[]){"127.0.0.1", "postgres", NULL});
  reactor_postgres_client_limits(&app.client, 0, p);
  for (i = 0; i < n; i ++)
    {
      app.queries ++;
      q = malloc(sizeof *q);
      q->app = &app;
      reactor_postgres_client_query_open(&q->query, query_event, q, &app.client, command);
    }
  reactor_core_run();
  reactor_core_destruct();
}

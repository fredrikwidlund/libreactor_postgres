#include <stdio.h>
#include <stdlib.h>
#include <netdb.h>
#include <sys/queue.h>

#include <libpq-fe.h>
#include <dynamic.h>
#include <reactor.h>

#include "reactor_postgres.h"
#include "reactor_postgres_client.h"

static void reactor_postgres_client_error(reactor_postgres_client *client)
{
  (void) fprintf(stderr, "error, client %p\n", (void *) client);
}

void reactor_postgres_client_dequeue(reactor_postgres_client *client)
{
  reactor_postgres_client_connection *connection;
  reactor_postgres_client_query *query;

  while (!TAILQ_EMPTY(&client->connections_available) && !TAILQ_EMPTY(&client->queries_waiting))
    {
      connection = TAILQ_FIRST(&client->connections_available);
      TAILQ_REMOVE(&client->connections_available, connection, entries);
      TAILQ_INSERT_TAIL(&client->connections_busy, connection, entries);
      client->stats.connections_busy ++;

      query = TAILQ_FIRST(&client->queries_waiting);
      TAILQ_REMOVE(&client->queries_waiting, query, entries);
      TAILQ_INSERT_TAIL(&client->queries_running, query, entries);

      connection->query = query;
      reactor_postgres_send(&connection->postgres, query->command);
    }
}

static void reactor_postgres_client_connection_available(reactor_postgres_client *client,
                                                         reactor_postgres_client_connection *connection)
{
  client->stats.connections_busy --;
  TAILQ_REMOVE(&client->connections_busy, connection, entries);
  TAILQ_INSERT_TAIL(&client->connections_available, connection, entries);
}

static void reactor_postgres_client_connection_event(void *state, int type, void *data)
{
  reactor_postgres_client_connection *c = state;

  switch (type)
    {
    case REACTOR_POSTGRES_EVENT_ERROR:
      reactor_postgres_client_error(c->client);
      break;
    case REACTOR_POSTGRES_EVENT_READY:
      reactor_postgres_client_connection_available(c->client, c);
      reactor_postgres_client_dequeue(c->client);
      break;
    case REACTOR_POSTGRES_EVENT_CLOSE:
      reactor_postgres_client_release(c->client);
      free(c);
      break;
    case REACTOR_POSTGRES_EVENT_QUERY_BAD:
      if (c->query)
        reactor_user_dispatch(&c->query->user, REACTOR_POSTGRES_CLIENT_QUERY_EVENT_BAD, data);
      break;
    case REACTOR_POSTGRES_EVENT_RESULT:
      if (c->query)
        reactor_user_dispatch(&c->query->user, REACTOR_POSTGRES_CLIENT_QUERY_EVENT_RESULT, data);
      break;
    case REACTOR_POSTGRES_EVENT_QUERY_DONE:
      reactor_postgres_client_connection_available(c->client, c);
      if (c->query)
        {
          c->client->stats.queries_completed ++;
          TAILQ_REMOVE(&c->client->queries_running, c->query, entries);
          reactor_user_dispatch(&c->query->user, REACTOR_POSTGRES_CLIENT_QUERY_EVENT_CLOSE, NULL);
          c->query = NULL;
        }
      reactor_postgres_client_dequeue(c->client);
      break;
    default:
      break;
    }
}

static void reactor_postgres_client_grow(reactor_postgres_client *client)
{
  reactor_postgres_client_connection *c;

  if (client->connections >= client->connections_max)
    return;

  c = malloc(sizeof *c);
  if (!c)
    {
      reactor_postgres_client_error(client);
      return;
    }
  c->client = client;
  c->query = NULL;
  TAILQ_INSERT_TAIL(&client->connections_busy, c, entries);
  client->connections ++;
  client->stats.connections ++;
  client->stats.connections_busy ++;
  reactor_postgres_client_hold(client);
  reactor_postgres_open(&c->postgres, reactor_postgres_client_connection_event, c, client->keys, client->values);
}

static void reactor_postgres_client_abort(reactor_postgres_client *client)
{
  reactor_postgres_client_connection *c;
  reactor_postgres_client_query *q;

  TAILQ_FOREACH(q, &client->queries_running, entries)
    {
      TAILQ_REMOVE(&client->queries_running, q, entries);
      reactor_user_dispatch(&q->user, REACTOR_POSTGRES_CLIENT_QUERY_EVENT_ABORT, NULL);
      reactor_user_dispatch(&q->user, REACTOR_POSTGRES_CLIENT_QUERY_EVENT_CLOSE, NULL);
    }
  TAILQ_FOREACH(q, &client->queries_waiting, entries)
    {
      TAILQ_REMOVE(&client->queries_waiting, q, entries);
      reactor_user_dispatch(&q->user, REACTOR_POSTGRES_CLIENT_QUERY_EVENT_ABORT, NULL);
      reactor_user_dispatch(&q->user, REACTOR_POSTGRES_CLIENT_QUERY_EVENT_CLOSE, NULL);
    }
  TAILQ_FOREACH(c, &client->connections_busy, entries)
    if (c->query)
      c->query = NULL;
}

void reactor_postgres_client_hold(reactor_postgres_client *client)
{
  client->ref ++;
}

void reactor_postgres_client_release(reactor_postgres_client *client)
{
  client->ref --;
  if (!client->ref)
    reactor_user_dispatch(&client->user, REACTOR_POSTGRES_CLIENT_EVENT_CLOSE, NULL);
}

void reactor_postgres_client_open(reactor_postgres_client *client, reactor_user_callback *callback, void *state,
                                  const char **keys, const char **values)
{
  *client = (reactor_postgres_client) {0};
  reactor_user_construct(&client->user, callback, state);
  client->keys = keys;
  client->values = values;
  client->connections = 0;
  TAILQ_INIT(&client->connections_available);
  TAILQ_INIT(&client->connections_busy);
  TAILQ_INIT(&client->queries_running);
  TAILQ_INIT(&client->queries_waiting);
  reactor_postgres_client_hold(client);
  reactor_postgres_client_limits(client, 0, REACTOR_POSTGRES_CLIENT_CONNECTIONS_MAX);
}

void reactor_postgres_client_limits(reactor_postgres_client *client, int min, int max)
{
  int i;

  client->connections_max = max;
  for (i = client->connections; i < min; i ++)
    reactor_postgres_client_grow(client);
}

void reactor_postgres_client_query_open(reactor_postgres_client_query *query, reactor_user_callback *callback, void *state,
                                        reactor_postgres_client *client, char *command)
{
  reactor_user_construct(&query->user, callback, state);
  query->command = command;
  client->stats.queries_queued ++;
  TAILQ_INSERT_TAIL(&client->queries_waiting, query, entries);
  if (!TAILQ_EMPTY(&client->connections_available))
    reactor_postgres_client_dequeue(client);
  else
    reactor_postgres_client_grow(client);
}

void reactor_postgres_client_close(reactor_postgres_client *client)
{
  reactor_postgres_client_connection *c;

  if (client->state != REACTOR_POSTGRES_CLIENT_STATE_OPEN)
    return;

  client->state = REACTOR_POSTGRES_CLIENT_STATE_CLOSED;
  reactor_postgres_client_abort(client);
  while (!TAILQ_EMPTY(&client->connections_busy))
    {
      c = TAILQ_FIRST(&client->connections_busy);
      TAILQ_REMOVE(&client->connections_busy, c, entries);
      reactor_postgres_close(&c->postgres);
    }
  while (!TAILQ_EMPTY(&client->connections_available))
    {
      c = TAILQ_FIRST(&client->connections_available);
      TAILQ_REMOVE(&client->connections_available, c, entries);
      reactor_postgres_close(&c->postgres);
     }

  reactor_postgres_client_release(client);
}

void reactor_postgres_client_get_stats(reactor_postgres_client *client, reactor_postgres_client_stats *stats)
{
  *stats = client->stats;
}

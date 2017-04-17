#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <netdb.h>
#include <errno.h>
#include <poll.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/queue.h>

#include <libpq-fe.h>
#include <dynamic.h>
#include <reactor.h>

#include "reactor_postgres.h"

void reactor_postgres_hold(reactor_postgres *pg)
{
  pg->ref ++;
}

void reactor_postgres_release(reactor_postgres *pg)
{
  pg->ref --;
  if (!pg->ref)
    {
      if (pg->connection)
        {
          if (pg->socket >= 0)
            reactor_core_fd_deregister(pg->socket);
          pg->socket = -1;
          PQfinish(pg->connection);
          pg->connection = NULL;
        }
      reactor_user_dispatch(&pg->user, REACTOR_POSTGRES_EVENT_CLOSE, NULL);
    }
}

static void reactor_postgres_error(reactor_postgres *pg)
{
  char *error = pg->connection ? PQerrorMessage(pg->connection) : NULL;

  ((struct pollfd *) reactor_core_fd_poll(pg->socket))->events = 0;
  pg->state = REACTOR_POSTGRES_STATE_ERROR;
  reactor_user_dispatch(&pg->user, REACTOR_POSTGRES_EVENT_ERROR, error);
}

static void reactor_postgres_state(reactor_postgres *p)
{
  int state_saved;

  if (!(p->state & (REACTOR_POSTGRES_STATE_CONNECTING |
                    REACTOR_POSTGRES_STATE_BUSY |
                    REACTOR_POSTGRES_STATE_AVAILABLE)))
    return;

  ((struct pollfd *) reactor_core_fd_poll(p->socket))->events = 0;

  switch (PQconnectPoll(p->connection))
    {
    case PGRES_POLLING_FAILED:
      reactor_postgres_error(p);
      return;
    case PGRES_POLLING_WRITING:
      ((struct pollfd *) reactor_core_fd_poll(p->socket))->events = POLLOUT;
      break;
    case PGRES_POLLING_READING:
      ((struct pollfd *) reactor_core_fd_poll(p->socket))->events = POLLIN;
      break;
    case PGRES_POLLING_OK:
      ((struct pollfd *) reactor_core_fd_poll(p->socket))->events = POLLIN;
      if (p->state == REACTOR_POSTGRES_STATE_CONNECTING)
        p->state = REACTOR_POSTGRES_STATE_BUSY;
      break;
    case PGRES_POLLING_ACTIVE:
      break;
    }

  if (p->state == REACTOR_POSTGRES_STATE_CONNECTING)
    return;

  if (PQisBusy(p->connection))
    p->state = REACTOR_POSTGRES_STATE_BUSY;
  else
    {
      state_saved = p->state;
      p->state = REACTOR_POSTGRES_STATE_AVAILABLE;
      if (state_saved == REACTOR_POSTGRES_STATE_BUSY)
        reactor_user_dispatch(&p->user, REACTOR_POSTGRES_EVENT_READY, NULL);
    }
}

static void reactor_postgres_read(reactor_postgres *p)
{
  int status, broken = 0;
  PGresult *result;

  status = PQconsumeInput(p->connection);
  if (!status)
    {
      reactor_postgres_error(p);
      return;
    }

  reactor_postgres_hold(p);
  while (1)
    {
      result = PQgetResult(p->connection);
      if (!result)
        break;
      switch (PQresultStatus(result))
        {
        case PGRES_SINGLE_TUPLE:
        case PGRES_TUPLES_OK:
        case PGRES_COMMAND_OK:
          reactor_user_dispatch(&p->user, REACTOR_POSTGRES_EVENT_RESULT, result);
          break;
        case PGRES_FATAL_ERROR:
          broken = 1;
          break;
        default:
          reactor_user_dispatch(&p->user, REACTOR_POSTGRES_EVENT_QUERY_BAD, PQerrorMessage(p->connection));
          break;
        }
      PQclear(result);
    }

  if (!PQisBusy(p->connection))
    p->state = REACTOR_POSTGRES_STATE_AVAILABLE;
  reactor_user_dispatch(&p->user, REACTOR_POSTGRES_EVENT_QUERY_DONE, NULL);

  if (broken)
    reactor_postgres_error(p);
  reactor_postgres_state(p);
  reactor_postgres_release(p);
}

static void reactor_postgres_event(void *state, int type, void *data)
{
  reactor_postgres *p = state;
  short revents = ((struct pollfd *) data)->revents;

  (void) type;
  switch (p->state)
    {
    case REACTOR_POSTGRES_STATE_CONNECTING:
      reactor_postgres_state(p);
      break;
    case REACTOR_POSTGRES_STATE_BUSY:
    case REACTOR_POSTGRES_STATE_AVAILABLE:
      if (revents & POLLIN)
        reactor_postgres_read(p);
      break;
    }
}

void reactor_postgres_open(reactor_postgres *p, reactor_user_callback *callback, void *state,
                           const char **keys, const char **values)
{
  p->ref = 0;
  p->state = REACTOR_POSTGRES_STATE_CONNECTING;
  reactor_user_construct(&p->user, callback, state);
  p->connection = PQconnectStartParams(keys, values, 0);
  PQsetnonblocking(p->connection, 1);
  p->socket = PQsocket(p->connection);
  reactor_core_fd_register(p->socket, reactor_postgres_event, p, 0);
  reactor_postgres_hold(p);
  reactor_postgres_state(p);
}

void reactor_postgres_close(reactor_postgres *pg)
{
  if (pg->state & REACTOR_POSTGRES_STATE_CLOSED)
    return;

  pg->state = REACTOR_POSTGRES_STATE_CLOSED;
  reactor_postgres_release(pg);
}

void reactor_postgres_send(reactor_postgres *p, char *query)
{
  int status;

  if (p->state != REACTOR_POSTGRES_STATE_AVAILABLE)
    {
      reactor_postgres_error(p);
      return;
    }

  status = PQsendQuery(p->connection, query);
  if (!status)
    {
      reactor_postgres_error(p);
      return;
    }
}

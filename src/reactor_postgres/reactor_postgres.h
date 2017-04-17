#ifndef REACTOR_POSTGRES_H_INCLUDED
#define REACTOR_POSTGRES_H_INCLUDED

enum reactor_postgres_state
{
  REACTOR_POSTGRES_STATE_CLOSED     = 0x01,
  REACTOR_POSTGRES_STATE_CONNECTING = 0x02,
  REACTOR_POSTGRES_STATE_BUSY       = 0x04,
  REACTOR_POSTGRES_STATE_AVAILABLE  = 0x08,
  REACTOR_POSTGRES_STATE_ERROR      = 0x10
};

enum reactor_postgres_event
{
  REACTOR_POSTGRES_EVENT_ERROR,
  REACTOR_POSTGRES_EVENT_CLOSE,
  REACTOR_POSTGRES_EVENT_READY,
  REACTOR_POSTGRES_EVENT_RESULT,
  REACTOR_POSTGRES_EVENT_QUERY_BAD,
  REACTOR_POSTGRES_EVENT_QUERY_DONE
};

typedef struct reactor_postgres reactor_postgres;
struct reactor_postgres
{
  short         ref;
  short         state;
  reactor_user  user;
  PGconn       *connection;
  int           socket;
};

void reactor_postgres_open(reactor_postgres *, reactor_user_callback *, void *, const char **, const char **);
void reactor_postgres_close(reactor_postgres *);
void reactor_postgres_send(reactor_postgres *, char *);

#endif /* REACTOR_POSTGRES_H_INCLUDED */

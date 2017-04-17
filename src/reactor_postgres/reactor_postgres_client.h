#ifndef REACTOR_POSTGRES_CLIENT_H_INCLUDED
#define REACTOR_POSTGRES_CLIENT_H_INCLUDED

#ifndef REACTOR_POSTGRES_CLIENT_CONNECTIONS_MAX
#define REACTOR_POSTGRES_CLIENT_CONNECTIONS_MAX 1024
#endif

enum reactor_postgres_client_event
{
  REACTOR_POSTGRES_CLIENT_EVENT_ERROR,
  REACTOR_POSTGRES_CLIENT_EVENT_CLOSE
};

enum reactor_postgres_client_state
{
  REACTOR_POSTGRES_CLIENT_STATE_OPEN,
  REACTOR_POSTGRES_CLIENT_STATE_CLOSED
};

enum reactor_postgres_client_query_event
{
  REACTOR_POSTGRES_CLIENT_QUERY_EVENT_BAD,
  REACTOR_POSTGRES_CLIENT_QUERY_EVENT_RESULT,
  REACTOR_POSTGRES_CLIENT_QUERY_EVENT_ABORT,
  REACTOR_POSTGRES_CLIENT_QUERY_EVENT_CLOSE
};

typedef struct reactor_postgres_client reactor_postgres_client;
typedef struct reactor_postgres_client_stats reactor_postgres_client_stats;
typedef struct reactor_postgres_client_connection reactor_postgres_client_connection;
typedef struct reactor_postgres_client_query reactor_postgres_client_query;

struct reactor_postgres_client_stats
{
  size_t                                            queries_queued;
  size_t                                            queries_completed;
  size_t                                            connections;
  size_t                                            connections_busy;
};

struct reactor_postgres_client
{
  int                                               ref;
  int                                               state;
  reactor_user                                      user;
  const char **                                     keys;
  const char **                                     values;
  int                                               connections_max;
  int                                               connections;
  TAILQ_HEAD(, reactor_postgres_client_connection)  connections_available;
  TAILQ_HEAD(, reactor_postgres_client_connection)  connections_busy;
  TAILQ_HEAD(, reactor_postgres_client_query)       queries_running;
  TAILQ_HEAD(, reactor_postgres_client_query)       queries_waiting;
  reactor_postgres_client_stats                     stats;
};

struct reactor_postgres_client_connection
{
  reactor_postgres_client                          *client;
  reactor_postgres                                  postgres;
  reactor_postgres_client_query                    *query;
  TAILQ_ENTRY(reactor_postgres_client_connection)   entries;
};

struct reactor_postgres_client_query
{
  reactor_user                                      user;
  char                                             *command;
  TAILQ_ENTRY(reactor_postgres_client_query)        entries;
};

void reactor_postgres_client_hold(reactor_postgres_client *);
void reactor_postgres_client_release(reactor_postgres_client *);
void reactor_postgres_client_open(reactor_postgres_client *, reactor_user_callback *, void *,
                                  const char **, const char **);
void reactor_postgres_client_limits(reactor_postgres_client *, int, int);
void reactor_postgres_client_query_open(reactor_postgres_client_query *, reactor_user_callback *, void *,
                                        reactor_postgres_client *, char *);
void reactor_postgres_client_close(reactor_postgres_client *);
void reactor_postgres_client_get_stats(reactor_postgres_client *, reactor_postgres_client_stats *);

#endif /* REACTOR_POSTGRES_CLIENT_H_INCLUDED */

/*
 * friendlist.c - [Starting code for] a web-based friend-graph manager.
 *
 * Based on:
 *  tiny.c - A simple, iterative HTTP/1.0 Web server that uses the 
 *      GET method to serve static and dynamic content.
 *   Tiny Web server
 *   Dave O'Hallaron
 *   Carnegie Mellon University
 */
#include "csapp.h"
#include "dictionary.h"
#include "more_string.h"
#include <stdlib.h>

static void doit(int fd);
static dictionary_t *read_requesthdrs(rio_t *rp);
static void read_postquery(rio_t *rp, dictionary_t *headers, dictionary_t *d);
static void clienterror(int fd, char *cause, char *errnum, 
                        char *shortmsg, char *longmsg);
static void print_stringdictionary(dictionary_t *d);

static void serve_greet(int fd, dictionary_t *query);
static void serve_befriend(int fd, dictionary_t* query);
static void serve_friends(int fd, dictionary_t* query);
static void serve_unfriend(int fd, dictionary_t* query);
static void serve_introduce(int fd, dictionary_t* query);

static char are_friends(char* user1, char* user2);
static char user_exists(char* user);
static void print_friends(int fd, char* user);
static void befriend(char* user1, char* user2);

static void* thread_work(void* arg);

static dictionary_t* friend_dict;
static dictionary_t* users_added;

char* server_host = NULL;
char* server_port = NULL;

static sem_t sem;

#define MAX_THREADS 16384

static pthread_t threads[MAX_THREADS];
static int free_thread = 0;

#define FRIEND_GET(key, index) dictionary_get((dictionary_t*)dictionary_get(friend_dict, key), index)
#define FRIEND_SET(key, index, value) dictionary_set((dictionary_t*)dictionary_get(friend_dict, key),index, value)
#define FRIEND_REMOVE(key, index) dictionary_remove((dictionary_t*)dictionary_get(friend_dict, key), index)

#define USERS_ADDED_VALUE(user) *((int*)dictionary_get(users_added, user))

int main(int argc, char **argv) 
{
  int listenfd, connfd;
  char hostname[MAXLINE], port[MAXLINE];
  socklen_t clientlen;
  struct sockaddr_storage clientaddr;

  /* Check command line args */
  if (argc != 2) {
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(1);
  }

  listenfd = Open_listenfd(argv[1]);

  /* Don't kill the server if there's an error, because
     we want to survive errors due to a client. But we
     do want to report errors. */
  exit_on_error(0);

  /* Also, don't stop on broken connections: */
  Signal(SIGPIPE, SIG_IGN);

  friend_dict = make_dictionary(0, free);
  users_added = make_dictionary(0, free);

  Sem_init(&sem, 0, 1);

  server_port = strdup(argv[1]);

  while (1) {
    clientlen = sizeof(clientaddr);
    connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);
    if (connfd >= 0) {
      Getnameinfo((SA *) &clientaddr, clientlen, hostname, MAXLINE, 
                  port, MAXLINE, 0);
      printf("Accepted connection from (%s, %s)\n", hostname, port);
      //doit(connfd);
      //Close(connfd);
      if(server_host != NULL)
        free(server_host);
      
      server_host = strdup(hostname);
      int* con = malloc(sizeof(int));
      *con = connfd;
      Pthread_create(&(threads[free_thread]), NULL, thread_work, con);
      //Pthread_join(threads[free_thread], NULL);
    }
  }

  for(int i = 0; i < MAX_THREADS; i++){
    Pthread_join(threads[i], NULL);
  }

  free_dictionary(friend_dict);
  free_dictionary(users_added);
  Sem_destroy(&sem);

  if(server_host != NULL)
      free(server_host);
  if(port != NULL)
       free(server_port);

}

void* thread_work(void* arg){

  doit(*((int*)(arg)));
  Close(*((int*)(arg)));
  free(arg);

  return NULL;

}

/*
 * doit - handle one HTTP request/response transaction
 */
void doit(int fd) 
{
  char buf[MAXLINE], *method, *uri, *version;
  rio_t rio;
  dictionary_t *headers, *query;

  /* Read request line and headers */
  Rio_readinitb(&rio, fd);
  if (Rio_readlineb(&rio, buf, MAXLINE) <= 0)
    return;
  printf("query buffer:\n");
  printf("%s", buf);
  
  if (!parse_request_line(buf, &method, &uri, &version)) {
    clienterror(fd, method, "400", "Bad Request",
                "Friendlist did not recognize the request");
  } else {
    if (strcasecmp(version, "HTTP/1.0")
        && strcasecmp(version, "HTTP/1.1")) {
      clienterror(fd, version, "501", "Not Implemented",
                  "Friendlist does not implement that version");
    } else if (strcasecmp(method, "GET")
               && strcasecmp(method, "POST")) {
      clienterror(fd, method, "501", "Not Implemented",
                  "Friendlist does not implement that method");
    } else {
      headers = read_requesthdrs(&rio);

      /* Parse all query arguments into a dictionary */
      query = make_dictionary(COMPARE_CASE_SENS, free);
      parse_uriquery(uri, query);
      if (!strcasecmp(method, "POST"))
        read_postquery(&rio, headers, query);

      /* For debugging, print the dictionary */
      print_stringdictionary(query);

      /* You'll want to handle different queries here,
         but the intial implementation always returns
         nothing: */
      P(&sem);
      free_thread++;
      if(free_thread > MAX_THREADS)
        free_thread = 0;
        
      //printf("HEY\n%s\n",uri);
      if(starts_with("/befriend", uri))
        serve_befriend(fd, query);
      else if (starts_with("/greet", uri))
        serve_greet(fd, query);
      else if (starts_with("/friends", uri))
        serve_friends(fd, query);
      else if (starts_with("/unfriend", uri))
        serve_unfriend(fd, query);
      else if (starts_with("/introduce", uri))
        serve_introduce(fd, query);
      else
       clienterror(fd, method, "404", "Not Found",
                  "Friendlist does not have that command");
      V(&sem);

      /* Clean up */
      free_dictionary(query);
      free_dictionary(headers);
    }

    /* Clean up status line */
    free(method);
    free(uri);
    free(version);
  }
}

/*
 * read_requesthdrs - read HTTP request headers
 */
dictionary_t *read_requesthdrs(rio_t *rp)
{
  char buf[MAXLINE];
  dictionary_t *d = make_dictionary(COMPARE_CASE_INSENS, free);

  Rio_readlineb(rp, buf, MAXLINE);
  printf("%s", buf);
  while(strcmp(buf, "\r\n")) {
    Rio_readlineb(rp, buf, MAXLINE);
    printf("%s", buf);
    parse_header_line(buf, d);
  }
  
  return d;
}

void read_postquery(rio_t *rp, dictionary_t *headers, dictionary_t *dest)
{
  char *len_str, *type, *buffer;
  int len;
  
  len_str = dictionary_get(headers, "Content-Length");
  len = (len_str ? atoi(len_str) : 0);

  type = dictionary_get(headers, "Content-Type");
  
  buffer = malloc(len+1);
  Rio_readnb(rp, buffer, len);
  buffer[len] = 0;

  if (!strcasecmp(type, "application/x-www-form-urlencoded")) {
    parse_query(buffer, dest);
  }

  free(buffer);
}

static char *ok_header(size_t len, const char *content_type) {
  char *len_str, *header;
  
  header = append_strings("HTTP/1.0 200 OK\r\n",
                          "Server: Friendlist Web Server\r\n",
                          "Connection: close\r\n",
                          "Content-length: ", len_str = to_string(len), "\r\n",
                          "Content-type: ", content_type, "\r\n\r\n",
                          NULL);
  free(len_str);

  return header;
}

/*
 * serve_request - example request handler
 */
static void serve_request(int fd, dictionary_t *query)
{
  size_t len;
  char *body, *header;

  body = strdup("alice\nbob");

  len = strlen(body);

  /* Send response headers to client */
  header = ok_header(len, "text/html; charset=utf-8");
  Rio_writen(fd, header, strlen(header));
  printf("Response headers:\n");
  printf("%s", header);

  free(header);

  /* Send response body to client */
  Rio_writen(fd, body, len);

  free(body);
}

static void serve_greet(int fd, dictionary_t* query)
{
    size_t len;
    char *body, *header;

    char* user = dictionary_get(query, "user");

    //append_strings calls malloc()
    body = append_strings("Greetings, ", user, ".", NULL);

    len = strlen(body);

  /* Send response headers to client */
  header = ok_header(len, "text/html; charset=utf-8");
  Rio_writen(fd, header, strlen(header));
  printf("Response headers:\n");
  printf("%s", header);

  free(header);

  /* Send response body to client */
  Rio_writen(fd, body, len);

  free(body);


}

static void serve_befriend(int fd, dictionary_t* query)
{
    char* user = dictionary_get(query, "user");
    char* friends = dictionary_get(query, "friends");
    //printf("%s %s",user,friends);

    dictionary_t* user_friends = (dictionary_t*)dictionary_get(friend_dict, user);

    //make new dictionary if there is none
    if(user_friends == NULL){
        //printf("makin dict\n");
      dictionary_t* list = make_dictionary(0, free);
      dictionary_set(friend_dict, user, list);
    }

    user_friends = (dictionary_t*)dictionary_get(friend_dict, user);
    //printf("%p", user_friends);

    //printf("\n%s\n",dictionary_get((dictionary_t*)dictionary_get(friend_dict, user), 0));

    char** names = split_string(friends, '\n');
    int i = 0;
    while(names[i] != NULL){
      befriend(user, names[i]);
      i++;
    }
      //printf("hello\n");

      //free(names);

      //printf("\n???%s\n",FRIEND_GET("me", "0"));
      print_friends(fd, user);

     // printf("hello\n");

}

static void serve_friends(int fd, dictionary_t* query){

  print_friends(fd, dictionary_get(query, "user"));

}

static void serve_unfriend(int fd, dictionary_t* query){

  char* user = dictionary_get(query, "user");
  char* friends = dictionary_get(query, "friends");

  dictionary_t* user_friends = (dictionary_t*)dictionary_get(friend_dict, user);

    //return if there is no dictionary
    if(user_friends == NULL){
      //char* title = append_strings("", NULL);
      char* header = ok_header(0, "text/html; charset=utf-8");
      Rio_writen(fd, header, strlen(header));
      //Rio_writen(fd, title, strlen(title));
      free(header);
      //free(title);
      return;
    }

    char** names = split_string(friends, '\n');
    int i = 0;
    const char** keys = dictionary_keys(user_friends);
    while(names[i] != NULL){

      int j = 0;
      char removed = 0;
      while(keys[j] != NULL){
        //printf("%s\n",keys[j]);
        char* c = dictionary_get(user_friends, keys[j]);
        //printf("user %d %s\n",j,c);
        if(c != NULL){
          if(starts_with(c, names[i])){
            FRIEND_REMOVE(user, keys[j]);
            removed++;
            //printf("%s and %s are no longer friends\n", user, names[i]);
            break;
          }
        }
        j++;
      }

      dictionary_t* friend_friends = (dictionary_t*)dictionary_get(friend_dict, names[i]);
      const char** fkeys = dictionary_keys(friend_friends);
      j = 0;
      while(fkeys[j] != NULL){

        //printf("%s\n",fkeys[j]);
        char* c = dictionary_get(friend_friends, fkeys[j]);
       // printf("fren %d %s\n",j,c);
        if(c != NULL){
          if(starts_with(c, user)){
            FRIEND_REMOVE(names[i], fkeys[j]);
          removed++;
            //printf("%s and %s are no longer friends\n", names[i], user);
            break;
          }
        }
        j++;
      }
      free(fkeys);

      if(removed == 2){
        printf("%s and %s have been successfully unfriended.\n", user, names[i]);
      }
      else
        printf("%s and %s are not friends... or something went wrong\n", user, names[i]);

      i++;
    }
    free(keys);

    print_friends(fd, user);




}

static void serve_introduce(int fd, dictionary_t* query){


  rio_t rio;

  char* user = dictionary_get(query, "user");
  char* friend = dictionary_get(query, "friend");
  char* host = dictionary_get(query, "host");
  char* port = dictionary_get(query, "port");

  if(starts_with(port, server_port) && starts_with(host, server_host)){

    int i = 0;
    dictionary_t* dict_user = dictionary_get(friend_dict, user);
    dictionary_t* dict_friend = dictionary_get(friend_dict, friend);
    for(; i < dictionary_count(dict_friend); i++){
      befriend(user, dictionary_value(dict_friend, i));
    }

    for(i = 0; i < dictionary_count(dict_user); i++){
      befriend(friend, dictionary_value(dict_user, i));
    }

  }

  else{
    int client = Open_clientfd(host, port);

    if(client == -1){
      clienterror(fd, "Open_clientfd", "400", "Bad Request",
                  "Friendlist did not recognize the request");
      return;
    }

    char* request = query_encode(friend);
    char* r = append_strings("GET /friends?user=", request, " HTTP/1.1\r\n\r\n",NULL);

    char friendlist[MAXLINE];

    Rio_readinitb(&rio, client);

    //printf("hhhhhhh\n");

    Rio_writen(client, r, strlen(r));
    //int n = 0;
    //char* start = append_strings("",NULL);
    Rio_readlineb(&rio, friendlist, MAXLINE);
    //printf("AAAA%s",friendlist);
    while(strcmp(friendlist, "\r\n")){
        Rio_readlineb(&rio, friendlist, MAXLINE);
        //printf("AAAA%s",friendlist);
    }
    //printf("%d",n);
    char** splitfriends = split_string(friendlist, '\n');
    //free(start);
    free(r);
    free(request);

    befriend(user, friend);

    int i = 0;
    dictionary_t* dict = dictionary_get(friend_dict, user);
    while(splitfriends[i] != NULL){
      befriend(user, splitfriends[i]);
      i++;
    }
    for(int j = 0; j < dictionary_count(dict); j++){
        befriend(splitfriends[i], dictionary_value(dict, j));
    }
  }
  print_friends(fd, user);

}

/*
 * clienterror - returns an error message to the client
 */
void clienterror(int fd, char *cause, char *errnum, 
		 char *shortmsg, char *longmsg) 
{
  size_t len;
  char *header, *body, *len_str;

  printf("fd: %d\n", fd);

  body = append_strings("<html><title>Friendlist Error</title>",
                        "<body bgcolor=""ffffff"">\r\n",
                        errnum, " ", shortmsg,
                        "<p>", longmsg, ": ", cause,
                        "<hr><em>Friendlist Server</em>\r\n",
                        NULL);
  len = strlen(body);

  /* Print the HTTP response */
  header = append_strings("HTTP/1.0 ", errnum, " ", shortmsg, "\r\n",
                          "Content-type: text/html; charset=utf-8\r\n",
                          "Content-length: ", len_str = to_string(len), "\r\n\r\n",
                          NULL);
  free(len_str);
  
  Rio_writen(fd, header, strlen(header));
  Rio_writen(fd, body, len);

  free(header);
  free(body);
}

static void print_stringdictionary(dictionary_t *d)
{
  int i, count;

  count = dictionary_count(d);
  for (i = 0; i < count; i++) {
    printf("%s=%s\n",
           dictionary_key(d, i),
           (const char *)dictionary_value(d, i));
  }
  printf("\n");
}

static char are_friends(char* user1, char* user2){

  dictionary_t* dict = (dictionary_t*)dictionary_get(friend_dict, user1);

  if(dict == NULL)
    return 0;

  for(int n = 0; n < dictionary_count(dict); n++){

    //printf("\nval: %s\n",dictionary_value(dict, n));
    if(starts_with(dictionary_value(dict, n),user2))
      return 1;
  }

  return 0;

}

static char user_exists(char* user){

  const char** users = dictionary_keys(users_added);
  int i = 0;
  while(users[i] != NULL){
    if(starts_with(users[i], user)){
      free(users);
      return 1;
    }
    i++;
  }
  free(users);
  return 0;

}

static void print_friends(int fd, char* user){

  dictionary_t* dict = (dictionary_t *)dictionary_get(friend_dict, user);

  printf("fd: %d\n", fd);

  //const char** keys = dictionary_keys(dict);

  char* title = append_strings(NULL);

  //Rio_writen(fd, title, strlen(title));

  //free(title);

  if(dict != NULL){

        printf("count: %ld\n",dictionary_count(dict));

        if(dictionary_count(dict) != 0){
            if(dictionary_count(dict) == 1){
              title = append_strings((char*)dictionary_value(dict, 0), NULL);
            }
            else{
              for(int i = 0; i < dictionary_count(dict)-1; i++)
              {
                //char* temp = title;
                //free(title);
                //printf("name??? %s\n", temp);
                if(title == NULL){
                  title = append_strings((char*)dictionary_value(dict, i), "\n", NULL);
                }
                else{
                  char* old = strdup(title);
                  free(title);
                  title = append_strings(old, (char*)dictionary_value(dict, i), "\n", NULL);
                  free(old);
                }
                //printf("::%s\n",title);
                //free(friend);
              }
                char* old = strdup(title);
                free(title);
                title = append_strings(old, (char*)dictionary_value(dict, dictionary_count(dict)-1), NULL);
                free(old);
            }

        //printf("title: %s\n", title);

      //int a = 0;
     //  while(title[a] != NULL){
       //  printf("%d ",title[a]);
       //   a++;
       // }
       // printf("\n");

        char* header = ok_header(strlen(title), "text/html; charset=utf-8");
        char* all = append_strings(header, title, NULL);
        //Rio_writen(fd, header, strlen(header));

        //Rio_writen(fd, title, strlen(title));
        Rio_writen(fd, all, strlen(all));
        free(title);
        free(header);
        free(all);
      }

      else{
        char* header = ok_header(0, "text/html; charset=utf-8");
        Rio_writen(fd, header, strlen(header));
        free(header);
        free(title);
      }
    }
    else{
      char* header = ok_header(0, "text/html; charset=utf-8");
      Rio_writen(fd, header, strlen(header));
      free(header);
      free(title);
      return;
    }

    return;

  }

static void befriend(char* user1, char* user2){

  dictionary_t* user_friends = (dictionary_t*)dictionary_get(friend_dict, user1);

    //make new dictionary if there is none
    if(user_friends == NULL){
      dictionary_t* list = make_dictionary(0, free);
      dictionary_set(friend_dict, user1, list);
    }

  //printf("%d, %d\n",term,startindex);

      //extract user from friend list
      int* p = malloc(sizeof(int));
      if(!user_exists(user1)){
        *p = 0;
      }
      else
        *p = USERS_ADDED_VALUE(user1);
      char* index = to_string(*p);
      //printf("\n%s\n", name);

      //if users are not already friends, add them
      if(!starts_with(user1, user2) && !are_friends(user1, user2)){

        FRIEND_SET(user1, index, user2);
        *p = (*p)+1;
        dictionary_set(users_added, user1, p);
        printf("%s and %s are now friends!\n", user1, user2);

        if(dictionary_get(friend_dict, user2) == NULL){
          dictionary_set(friend_dict, user2, make_dictionary(0, free));
        }

          int* n = malloc(sizeof(int));
          if(!user_exists(user2)){
            *n = 0;
          }
          else
            *n = USERS_ADDED_VALUE(user2);
          char* ind = to_string(*n);

          //since "user" is on the stack, we need to create a copy on the heap
          // so we use strdup()
          FRIEND_SET(user2, ind, strdup(user1));
          *n = (*n)+1;
          dictionary_set(users_added, user2, n);
          printf("%s and %s are now friends!\n", user2, user1);
          free(ind);
      }
      else{
        free(p);
        printf("%s and %s are already friends\n",user1,user2);
      }
      //print_friends(fd, names[i]);
      free(index);

}

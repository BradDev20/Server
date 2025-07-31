USAGE GUIDE:

To execute this program, navigate to this folder in the terminal and type:

./friendlist <port>

Where <port> is a desired port number. Port numbers over 2000 are most likely to work.
Then, to send/recieve information, go to a browser of choice. Make sure to remember the port that the
server program is using.

The following lines are URLs to type into your browser search:
For greeting a user, type "localhost:<port>/greet?user=<user>"
For getting friends of a user, type "localhost:<port>/friends?user=<user>"

For befriending users, type "localhost:<port>/befriend?user=<user1>&friends=<user2>"
For unfriending users, type "localhost:<port>/unfriend?user=<user1>&friends=<user2>"

NOTE: the "friends" tag supports multiple users at once by adding "%0A" in between each new name.
For example, "localhost:2556/befriend?user=alice&friends=bob%0Acarol"
will cause "alice" to be friends of both "bob" and "carol".


For introducing two users to their friends, type:
"localhost:<port_a>/introduce?host=localhost&port=<port_b>&user=<user1>&friend=<user2>"

NOTE: This URL supports cross-server communication. "port_a" and "port_b" can be different port numbers,
as long as both are valid. Also make sure that "user1" exist in "port_a"'s server
and "user2" exists in "port_b"'s server.
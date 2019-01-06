RControl consists of the following parts:
* device (kernel module that queues and handles the requests and outputs them to the parallel port)
* server (serves clients by opening a service socket that can be used for control requests)
* client (ncurses based terminal client with full functionality)
* cgi-bin (client that can be used with a web server for simple temporary controls)


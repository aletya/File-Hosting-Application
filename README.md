# File Hosting Application
Alex Yang <br>
## Overview
A client-server application designed to manage public file hosting across networks using TCP.
## Usage
Server usage: ./server <port>
Client usage: 
- ./client <server IP>:<server port> GET [remote file name] [local file name]
- ./client <server IP>:<server port> PUT [remote file name] [local file name]
- ./client <server IP>:<server port> DELETE [remote file name]
- ./client <server IP>:<server port> LIST [remote file name] [local file name]
## Notes
Completed as the final assigment for System Programming.

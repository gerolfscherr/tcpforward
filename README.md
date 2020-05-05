simple program which listens on a port and forwards any incoming tcp streams to another server.
it is also possible to rewrite part of the streams or filter it or just dump it to the disk.
it can be used for network debugging like wireshark or making old devices work again.

todo: privilege dropping(!)
full parameter set
testing 
dumping to disk
* its single threaded and blocking

example usage
./tcpforward -b 192.168.8.234  -p 1234 -t 12.34.56.78 -r 80 -v
where -b specifies the local interface to listen to, -p the local port, -t the remote ip, -r the remote port

it has to be checked out in a directory next to the simpledns project bc it uses some files from there.

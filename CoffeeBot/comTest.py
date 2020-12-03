
import socket
import sys

HOST = '192.168.0.248'  # Standard loopback interface address (localhost)
PORT = 23        # Port to listen on (non-privileged ports are > 1023)

with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
    s.connect((HOST, PORT))
    s.sendall(b'Hello, world')
    data = s.recv(1024)
    print('Received', data)
    s.close()

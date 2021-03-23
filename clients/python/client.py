import socket

class PHDB:
    def __init__(self, host, port):
        self.s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.s.connect((host, port))
    
    def rebuild(self):
        command = 60001
        command_b = command.to_bytes(2, "little")
        self.s.sendall(command_b)
        data = self.s.recv(1024)
        code = int.from_bytes(data, "little")
        if code == 1:
            print("ok")
        else:
            print("error")
        return

    def query(self, hash_, n_results=8):
        n_results_b = n_results.to_bytes(2, "little")
        hash_b = hash_.to_bytes(8, "little")

        msg = n_results_b + hash_b
        
        #print(msg)

        header_offset = 16

        self.s.sendall(msg)
        data = self.s.recv(1024)
        resp_size  = data[0:4]
        resp_size_ = int.from_bytes(resp_size, "little")

        n = (resp_size_ - header_offset) / 13
        n_rem = (resp_size_ - header_offset) % 13

        if n_rem == 0 and n > 0:
            ihash      = data[4:12]
            query_time = data[12:16]
            n2 = int.from_bytes(ihash, "little")
            n3 = int.from_bytes(query_time, "little");

            print(">query:", n2)
            print(">query time:", n3)

            print("---")

            for i in range(int(n)):
                block_offset = 13 * i
                fset = block_offset + header_offset
                id    = data[fset     :fset + 4]
                rhash = data[fset + 4 :fset+12]
                dist  = data[fset + 12:fset+13]
             
                n4 = int.from_bytes(id, "little")
                n5 = int.from_bytes(rhash, "little")
                n6 = int.from_bytes(dist, "little")
                
                print("n =", i + 1, end=" ")
                print("id =", n4, end=" ")
                print("hash =", n5, end=" ")
                print("dist =", n6, end="")
                print("")

        else:
            print("corrupted")
       

connection = PHDB("127.0.0.1", 6969)

import time

connection.query(15634995779091902094, 8)
connection.rebuild()
connection.query(15634995779091902094, 8)

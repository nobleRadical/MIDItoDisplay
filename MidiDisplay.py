from simplecoremidi import send_midi, recv_midi
import threading, time, tomllib, socketserver

HOST = '0.0.0.0'
PORT = 8001

mapping = {}
patch_number = 0
patch_name = "???"

def config():
    global mapping
    with open("patch_mapping.toml", 'rb') as f:
        mapping = tomllib.load(f)

def parse_midi(data):
    global patch_number
    global patch_name
    print("MIDI<- ", end='')
    print(" ".join(hex(n) for n in data))
    for i in range(0,len(data)):
        byte = data[i]
        byte = byte & 0xF0 # zero channel bits
        if byte == 0xC0:
            # Program change: next byte is patch number
            patch_number = data[i+1]
            print("Patch number: " + str(patch_number))
            patch_name = str(patch_number) in mapping and mapping.get(str(patch_number)) or "???"
            print("Patch Name: " + patch_name)

#completely copied from https://gist.github.com/pklaus/c4c37152e261a9e9331f
class SingleTCPHandler(socketserver.BaseRequestHandler):
    "One instance per connection.  Override handle(self) to customize action."
    def handle(self):
        global patch_name
        # self.request is the client connection
        data = self.request.recv(1024)  # clip input at 1Kb
        text = data.decode('utf-8')
        print("Recieved Request: " + text)
        self.request.send(patch_name.encode('utf-8'))
        self.request.close()

class SimpleServer(socketserver.ThreadingMixIn, socketserver.TCPServer):
    # Ctrl-C will cleanly kill all spawned threads
    daemon_threads = True
    # much faster rebinding
    allow_reuse_address = True

    def __init__(self, server_address, RequestHandlerClass):
        socketserver.TCPServer.__init__(self, server_address, RequestHandlerClass)

def webserver():
    server = SimpleServer((HOST, PORT), SingleTCPHandler)
    threading.Thread(target=server.serve_forever, daemon=True).start()


def runtime():
    while True:
        data = recv_midi()
        threading.Thread(target=parse_midi, daemon=True, args=(data,)).start()
        time.sleep(1.0)

config()
webserver()
runtime()

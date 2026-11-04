import socket, sys, time
ip = sys.argv[1]                      # the ESP32's IP from the Serial Monitor
s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
while True:
    s.sendto(b"x" * 20, (ip, 4210))
    time.sleep(0.01)                  # ~90 packets/s
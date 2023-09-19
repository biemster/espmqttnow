import network
import espnow

# A WLAN interface must be active to send()/recv()
sta = network.WLAN(network.STA_IF)
sta.active(True)
sta.disconnect()   # Because ESP8266 auto-connects to last Access Point

e = espnow.ESPNow()
e.active(True)

mac = ''.join([hex(m) for m in sta.config("mac")])[2:].replace('0x',':')
print(f'listening for ESPNOW frames on {mac} channel {sta.config("channel")}')

while True:
    host, msg = e.recv()
    host_mac = ''.join([hex(m) for m in host])[2:].replace('0x',':')
    if msg:             # msg == None if timeout in recv()
        print(host_mac, msg)
        if msg == b'end':
            break

import network
import espnow

# A WLAN interface must be active to send()/recv()
sta = network.WLAN(network.STA_IF)  # Or network.AP_IF
sta.active(True)
sta.disconnect()      # For ESP8266

e = espnow.ESPNow()
e.active(True)
peer = b'\xff\xff\xff\xff\xff\xff'   # MAC address of peer's wifi interface
e.add_peer(peer)      # Must add_peer() before send()

mac = ''.join([hex(m) for m in sta.config("mac")])[2:].replace('0x',':')
print(f'sending ESPNOW frames on {mac} channel {sta.config("channel")}')

e.send(peer, "Starting...")
for i in range(3):
    e.send(peer, str(i)*20, True)
e.send(peer, b'end')